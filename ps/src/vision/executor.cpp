/****************************************************************
 * Copyright (c) 2023 Anlogic, Inc.  All rights reserved.
 * SPDX-License-Identifier: MIT
 ****************************************************************/

#include "armnnUtils/DataLayoutIndexed.hpp"
#include "executor.hpp"
#include <cstdlib>
#include "armnnALUtils/cma_mem_init.hpp"

// DragonEyeSorter: ARMNN_LOG=trace|debug|info|warning lowers the log threshold (diagnosing model loading)
static armnn::LogSeverity des_log_severity()
{
    const char *e = getenv("ARMNN_LOG");
    if (!e) return armnn::LogSeverity::Error;
    std::string s = e;
    if (s == "trace") return armnn::LogSeverity::Trace;
    if (s == "debug") return armnn::LogSeverity::Debug;
    if (s == "info") return armnn::LogSeverity::Info;
    if (s == "warning") return armnn::LogSeverity::Warning;
    return armnn::LogSeverity::Error;
}

// Both constructors used to create an IRuntime twice: once with the default options (thrown away at the next
// assignment) and once with the CMA allocator map. Create once here, with the allocator map installed before
// LoadNetwork allocates anything.
void NPUExecutor::prepare(armnn::INetworkPtr network, const std::vector<armnn::BackendId> &backends,
                          const std::optional<armnn::INetworkProperties> &network_properties)
{
    // Create ArmNN runtime
    armnn::IRuntime::CreationOptions rt_options; // default options
    rt_options.m_CustomAllocatorMap["CpuAcc"] = rt_options.m_CustomAllocatorMap["CpuRef"] =
        std::shared_ptr<armnn::ICustomAllocator>(new armnn::CmaAllocator);
    m_runtime = armnn::IRuntime::Create(rt_options);

    // Optimise ArmNN network
    armnn::OptimizerOptionsOpaque op_options;
    op_options.SetImportEnabled(true);
    op_options.SetExportEnabled(true);

    armnn::IOptimizedNetworkPtr optNet = armnn::Optimize(*network, backends, m_runtime->GetDeviceSpec(), op_options);

    // Load graph into runtime
    armnn::INetworkProperties networkProperties_default(
        false,
        armnn::MemorySource::Malloc, armnn::MemorySource::Malloc);
    std::string errorMsg;
    m_runtime->LoadNetwork(m_netId, std::move(optNet), errorMsg, network_properties.has_value() ? network_properties.value() : networkProperties_default);
}

// binding info -> one CMA buffer + tensor per input / output
void NPUExecutor::allocTensors()
{
    for (auto &info : m_inputBindingInfo) {
        m_inputBuffers.emplace_back(CmaBuf(info.second.GetNumBytes()));
        info.second.SetConstant(true);
        m_inputTensors.push_back({info.first, armnn::ConstTensor(info.second, m_inputBuffers.back()->virt_addr)});
    }

    for (const auto &info : m_outputBindingInfo) {
        m_outputBuffers.emplace_back(CmaBuf(info.second.GetNumBytes()));
        m_outputTensors.push_back({info.first, armnn::Tensor(info.second, m_outputBuffers.back()->virt_addr)});
    }
}

NPUExecutor::NPUExecutor(const std::string &modelPath, const std::vector<armnn::BackendId> &backends, std::optional<armnn::INetworkProperties> network_properties)
    : m_runtime(nullptr, nullptr) {   // empty until prepare() creates it with the CMA allocator map (IRuntimePtr has no default ctor)
    armnn::ConfigureLogging(true, false, des_log_severity());

    armnnOnnxParser::IOnnxParserPtr parser = armnnOnnxParser::IOnnxParser::Create();

    // Construct ArmNN network
    armnn::INetworkPtr network = parser->CreateNetworkFromBinaryFile(modelPath.c_str());

    prepare(std::move(network), backends, network_properties);

    for (const std::string &name : parser->GetInputTensorNames()) {
        m_inputBindingInfo.push_back(parser->GetNetworkInputBindingInfo(name));
        m_inputLayerNamesList.push_back(name);
    }
    for (const std::string &name : parser->GetOutputTensorNames()) {
        m_outputBindingInfo.push_back(parser->GetNetworkOutputBindingInfo(name));
        m_outputLayerNamesList.push_back(name);
    }

    allocTensors();
}

NPUExecutor::NPUExecutor(
    armnn::INetworkPtr network, std::vector<armnn::BindingPointInfo> inputBindingInfo, std::vector<armnn::BindingPointInfo> outputBindingInfo,
    const std::vector<armnn::BackendId> &backends, std::optional<armnn::INetworkProperties> network_properties)
    : m_runtime(nullptr, nullptr) {   // empty until prepare() creates it with the CMA allocator map (IRuntimePtr has no default ctor)
    armnn::ConfigureLogging(true, false, des_log_severity());

    prepare(std::move(network), backends, network_properties);

    for (const auto &i : inputBindingInfo) {
        m_inputBindingInfo.push_back(i);
        m_inputLayerNamesList.push_back(std::to_string(i.first));
    }
    for (const auto &i : outputBindingInfo) {
        m_outputBindingInfo.push_back(i);
        m_outputLayerNamesList.push_back(std::to_string(i.first));
    }

    allocTensors();
}

Size NPUExecutor::GetInputAspectRatio(int tensorIndex) {
	const auto shape = m_inputBindingInfo.at(tensorIndex).second.GetShape();
    assert(shape.GetNumDimensions() == 4);
    armnnUtils::DataLayoutIndexed nchw(armnn::DataLayout::NCHW);
    return Size(shape[nchw.GetWidthIndex()],
                shape[nchw.GetHeightIndex()]);
}

armnn::DataType NPUExecutor::GetInputDataType() const {
	assert(m_inputLayerNamesList.size() > 0);
	return m_inputBindingInfo.at(0).second.GetDataType();
}

float NPUExecutor::GetInputQuantizationScale(int tensorIndex) {
	return m_inputBindingInfo.at(tensorIndex).second.GetQuantizationScale();
}

int NPUExecutor::GetInputQuantizationOffset(int tensorIndex) {
	return m_inputBindingInfo.at(tensorIndex).second.GetQuantizationOffset();
}

float NPUExecutor::GetOutputQuantizationScale(int tensorIndex) {
	return m_outputBindingInfo.at(tensorIndex).second.GetQuantizationScale();
}

int NPUExecutor::GetOutputQuantizationOffset(int tensorIndex) {
	return m_outputBindingInfo.at(tensorIndex).second.GetQuantizationOffset();
}

CmaBuf& NPUExecutor::GetInputBuffer(int tensorIndex) {
	return m_inputBuffers.at(tensorIndex);
}

CmaBuf& NPUExecutor::GetOutputBuffer(int tensorIndex) {
	return m_outputBuffers.at(tensorIndex);
}

bool NPUExecutor::Run() {

    armnn::Status ret = m_runtime->EnqueueWorkload(m_netId, m_inputTensors, m_outputTensors);

    std::stringstream inferenceFinished;
    inferenceFinished << "Inference finished with code {" << log_as_int(ret) << "}\n";

    ARMNN_LOG(trace) << inferenceFinished.str();

    if (ret == armnn::Status::Failure)
    {
        ARMNN_LOG(error) << "Failed to perform inference.";
    }

    return (armnn::Status::Success == ret);
}

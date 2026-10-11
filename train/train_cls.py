#!/usr/bin/env python3
"""Train the part classifier: YOLOv8n-cls @ 224 on part crops (ps/app/roi_crop: 56 mm -> 224 px).

Dataset = ImageFolder: <data>/train/<id>_<label>/*.png|jpg and <data>/val/... (folder names sort in class-id order,
so the model's output index = models/label_list.json id). Made by synth_crops.py (smoke test) or
prepare_dataset.py (real crops from `des_app collect`).

Augmentation is deliberately mild: the defects are small (1~2 mm = 4~8 px), so no random erasing and almost no
random-resized-crop (both can remove the defect and teach the model wrong labels). Flips are free: the crop is
normalized to the part axis, which has a 180 deg ambiguity anyway.

    python train_cls.py --data data/synth --epochs 15 --name synth
"""
import argparse

from ultralytics import YOLO


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--model", default="yolov8n-cls.pt", help="pretrained weights (ImageNet) or a .yaml to train from scratch")
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--imgsz", type=int, default=224)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--device", default="cpu")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--project", default="runs")
    ap.add_argument("--name", default="cls")
    a = ap.parse_args()

    model = YOLO(a.model)
    model.train(
        data=a.data, epochs=a.epochs, imgsz=a.imgsz, batch=a.batch, device=a.device, workers=a.workers,
        project=a.project, name=a.name, exist_ok=True, patience=10, plots=True, seed=0, deterministic=True,
        # mild, defect-preserving augmentation
        fliplr=0.5, flipud=0.5, hsv_h=0.0, hsv_s=0.2, hsv_v=0.3, scale=0.1, erasing=0.0,
    )
    metrics = model.val(data=a.data, imgsz=a.imgsz, device=a.device)
    print(f"top1 {metrics.top1:.4f}  top5 {metrics.top5:.4f}")


if __name__ == "__main__":
    main()

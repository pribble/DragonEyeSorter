// 出处：南京维酷机器人科技（VLKUS）ISP 参考设计；GAMMA=2.2 查找表（8bit -> 12bit）。
// 单配置特化后仅保留 2.2 曲线（isp_top 写死 GAMMA_10x=22）；本轮只做数据表紧凑排版，数值逐项不变。
module lut_2_2 (
    input  wire        I_clk,
    input  wire        I_rst_n,

    input  wire [7:0]  I_LUT_2_2_data,
    output reg  [11:0] O_LUT_2_2_data
);

    always @(*) begin
        case (I_LUT_2_2_data)
            0: O_LUT_2_2_data = 240; 1: O_LUT_2_2_data = 396; 2: O_LUT_2_2_data = 499; 3: O_LUT_2_2_data = 582; 4: O_LUT_2_2_data = 652; 5: O_LUT_2_2_data = 715;
            6: O_LUT_2_2_data = 771; 7: O_LUT_2_2_data = 823; 8: O_LUT_2_2_data = 871; 9: O_LUT_2_2_data = 916; 10: O_LUT_2_2_data = 959; 11: O_LUT_2_2_data = 999;
            12: O_LUT_2_2_data = 1038; 13: O_LUT_2_2_data = 1075; 14: O_LUT_2_2_data = 1110; 15: O_LUT_2_2_data = 1145; 16: O_LUT_2_2_data = 1178; 17: O_LUT_2_2_data = 1209;
            18: O_LUT_2_2_data = 1240; 19: O_LUT_2_2_data = 1270; 20: O_LUT_2_2_data = 1300; 21: O_LUT_2_2_data = 1328; 22: O_LUT_2_2_data = 1356; 23: O_LUT_2_2_data = 1383;
            24: O_LUT_2_2_data = 1409; 25: O_LUT_2_2_data = 1435; 26: O_LUT_2_2_data = 1461; 27: O_LUT_2_2_data = 1485; 28: O_LUT_2_2_data = 1510; 29: O_LUT_2_2_data = 1534;
            30: O_LUT_2_2_data = 1557; 31: O_LUT_2_2_data = 1580; 32: O_LUT_2_2_data = 1603; 33: O_LUT_2_2_data = 1625; 34: O_LUT_2_2_data = 1647; 35: O_LUT_2_2_data = 1668;
            36: O_LUT_2_2_data = 1689; 37: O_LUT_2_2_data = 1710; 38: O_LUT_2_2_data = 1731; 39: O_LUT_2_2_data = 1751; 40: O_LUT_2_2_data = 1771; 41: O_LUT_2_2_data = 1791;
            42: O_LUT_2_2_data = 1810; 43: O_LUT_2_2_data = 1830; 44: O_LUT_2_2_data = 1849; 45: O_LUT_2_2_data = 1868; 46: O_LUT_2_2_data = 1886; 47: O_LUT_2_2_data = 1904;
            48: O_LUT_2_2_data = 1923; 49: O_LUT_2_2_data = 1940; 50: O_LUT_2_2_data = 1958; 51: O_LUT_2_2_data = 1976; 52: O_LUT_2_2_data = 1993; 53: O_LUT_2_2_data = 2010;
            54: O_LUT_2_2_data = 2027; 55: O_LUT_2_2_data = 2044; 56: O_LUT_2_2_data = 2061; 57: O_LUT_2_2_data = 2077; 58: O_LUT_2_2_data = 2094; 59: O_LUT_2_2_data = 2110;
            60: O_LUT_2_2_data = 2126; 61: O_LUT_2_2_data = 2142; 62: O_LUT_2_2_data = 2157; 63: O_LUT_2_2_data = 2173; 64: O_LUT_2_2_data = 2189; 65: O_LUT_2_2_data = 2204;
            66: O_LUT_2_2_data = 2219; 67: O_LUT_2_2_data = 2234; 68: O_LUT_2_2_data = 2249; 69: O_LUT_2_2_data = 2264; 70: O_LUT_2_2_data = 2279; 71: O_LUT_2_2_data = 2294;
            72: O_LUT_2_2_data = 2308; 73: O_LUT_2_2_data = 2322; 74: O_LUT_2_2_data = 2337; 75: O_LUT_2_2_data = 2351; 76: O_LUT_2_2_data = 2365; 77: O_LUT_2_2_data = 2379;
            78: O_LUT_2_2_data = 2393; 79: O_LUT_2_2_data = 2407; 80: O_LUT_2_2_data = 2421; 81: O_LUT_2_2_data = 2434; 82: O_LUT_2_2_data = 2448; 83: O_LUT_2_2_data = 2461;
            84: O_LUT_2_2_data = 2474; 85: O_LUT_2_2_data = 2488; 86: O_LUT_2_2_data = 2501; 87: O_LUT_2_2_data = 2514; 88: O_LUT_2_2_data = 2527; 89: O_LUT_2_2_data = 2540;
            90: O_LUT_2_2_data = 2553; 91: O_LUT_2_2_data = 2566; 92: O_LUT_2_2_data = 2578; 93: O_LUT_2_2_data = 2591; 94: O_LUT_2_2_data = 2604; 95: O_LUT_2_2_data = 2616;
            96: O_LUT_2_2_data = 2628; 97: O_LUT_2_2_data = 2641; 98: O_LUT_2_2_data = 2653; 99: O_LUT_2_2_data = 2665; 100: O_LUT_2_2_data = 2677; 101: O_LUT_2_2_data = 2690;
            102: O_LUT_2_2_data = 2702; 103: O_LUT_2_2_data = 2713; 104: O_LUT_2_2_data = 2725; 105: O_LUT_2_2_data = 2737; 106: O_LUT_2_2_data = 2749; 107: O_LUT_2_2_data = 2761;
            108: O_LUT_2_2_data = 2772; 109: O_LUT_2_2_data = 2784; 110: O_LUT_2_2_data = 2795; 111: O_LUT_2_2_data = 2807; 112: O_LUT_2_2_data = 2818; 113: O_LUT_2_2_data = 2830;
            114: O_LUT_2_2_data = 2841; 115: O_LUT_2_2_data = 2852; 116: O_LUT_2_2_data = 2863; 117: O_LUT_2_2_data = 2875; 118: O_LUT_2_2_data = 2886; 119: O_LUT_2_2_data = 2897;
            120: O_LUT_2_2_data = 2908; 121: O_LUT_2_2_data = 2919; 122: O_LUT_2_2_data = 2930; 123: O_LUT_2_2_data = 2940; 124: O_LUT_2_2_data = 2951; 125: O_LUT_2_2_data = 2962;
            126: O_LUT_2_2_data = 2973; 127: O_LUT_2_2_data = 2983; 128: O_LUT_2_2_data = 2994; 129: O_LUT_2_2_data = 3004; 130: O_LUT_2_2_data = 3015; 131: O_LUT_2_2_data = 3025;
            132: O_LUT_2_2_data = 3036; 133: O_LUT_2_2_data = 3046; 134: O_LUT_2_2_data = 3057; 135: O_LUT_2_2_data = 3067; 136: O_LUT_2_2_data = 3077; 137: O_LUT_2_2_data = 3087;
            138: O_LUT_2_2_data = 3098; 139: O_LUT_2_2_data = 3108; 140: O_LUT_2_2_data = 3118; 141: O_LUT_2_2_data = 3128; 142: O_LUT_2_2_data = 3138; 143: O_LUT_2_2_data = 3148;
            144: O_LUT_2_2_data = 3158; 145: O_LUT_2_2_data = 3168; 146: O_LUT_2_2_data = 3178; 147: O_LUT_2_2_data = 3188; 148: O_LUT_2_2_data = 3197; 149: O_LUT_2_2_data = 3207;
            150: O_LUT_2_2_data = 3217; 151: O_LUT_2_2_data = 3227; 152: O_LUT_2_2_data = 3236; 153: O_LUT_2_2_data = 3246; 154: O_LUT_2_2_data = 3255; 155: O_LUT_2_2_data = 3265;
            156: O_LUT_2_2_data = 3275; 157: O_LUT_2_2_data = 3284; 158: O_LUT_2_2_data = 3294; 159: O_LUT_2_2_data = 3303; 160: O_LUT_2_2_data = 3312; 161: O_LUT_2_2_data = 3322;
            162: O_LUT_2_2_data = 3331; 163: O_LUT_2_2_data = 3340; 164: O_LUT_2_2_data = 3350; 165: O_LUT_2_2_data = 3359; 166: O_LUT_2_2_data = 3368; 167: O_LUT_2_2_data = 3377;
            168: O_LUT_2_2_data = 3386; 169: O_LUT_2_2_data = 3396; 170: O_LUT_2_2_data = 3405; 171: O_LUT_2_2_data = 3414; 172: O_LUT_2_2_data = 3423; 173: O_LUT_2_2_data = 3432;
            174: O_LUT_2_2_data = 3441; 175: O_LUT_2_2_data = 3450; 176: O_LUT_2_2_data = 3459; 177: O_LUT_2_2_data = 3467; 178: O_LUT_2_2_data = 3476; 179: O_LUT_2_2_data = 3485;
            180: O_LUT_2_2_data = 3494; 181: O_LUT_2_2_data = 3503; 182: O_LUT_2_2_data = 3512; 183: O_LUT_2_2_data = 3520; 184: O_LUT_2_2_data = 3529; 185: O_LUT_2_2_data = 3538;
            186: O_LUT_2_2_data = 3546; 187: O_LUT_2_2_data = 3555; 188: O_LUT_2_2_data = 3564; 189: O_LUT_2_2_data = 3572; 190: O_LUT_2_2_data = 3581; 191: O_LUT_2_2_data = 3589;
            192: O_LUT_2_2_data = 3598; 193: O_LUT_2_2_data = 3606; 194: O_LUT_2_2_data = 3615; 195: O_LUT_2_2_data = 3623; 196: O_LUT_2_2_data = 3632; 197: O_LUT_2_2_data = 3640;
            198: O_LUT_2_2_data = 3648; 199: O_LUT_2_2_data = 3657; 200: O_LUT_2_2_data = 3665; 201: O_LUT_2_2_data = 3673; 202: O_LUT_2_2_data = 3682; 203: O_LUT_2_2_data = 3690;
            204: O_LUT_2_2_data = 3698; 205: O_LUT_2_2_data = 3706; 206: O_LUT_2_2_data = 3714; 207: O_LUT_2_2_data = 3723; 208: O_LUT_2_2_data = 3731; 209: O_LUT_2_2_data = 3739;
            210: O_LUT_2_2_data = 3747; 211: O_LUT_2_2_data = 3755; 212: O_LUT_2_2_data = 3763; 213: O_LUT_2_2_data = 3771; 214: O_LUT_2_2_data = 3779; 215: O_LUT_2_2_data = 3787;
            216: O_LUT_2_2_data = 3795; 217: O_LUT_2_2_data = 3803; 218: O_LUT_2_2_data = 3811; 219: O_LUT_2_2_data = 3819; 220: O_LUT_2_2_data = 3827; 221: O_LUT_2_2_data = 3835;
            222: O_LUT_2_2_data = 3843; 223: O_LUT_2_2_data = 3850; 224: O_LUT_2_2_data = 3858; 225: O_LUT_2_2_data = 3866; 226: O_LUT_2_2_data = 3874; 227: O_LUT_2_2_data = 3882;
            228: O_LUT_2_2_data = 3889; 229: O_LUT_2_2_data = 3897; 230: O_LUT_2_2_data = 3905; 231: O_LUT_2_2_data = 3912; 232: O_LUT_2_2_data = 3920; 233: O_LUT_2_2_data = 3928;
            234: O_LUT_2_2_data = 3935; 235: O_LUT_2_2_data = 3943; 236: O_LUT_2_2_data = 3951; 237: O_LUT_2_2_data = 3958; 238: O_LUT_2_2_data = 3966; 239: O_LUT_2_2_data = 3973;
            240: O_LUT_2_2_data = 3981; 241: O_LUT_2_2_data = 3988; 242: O_LUT_2_2_data = 3996; 243: O_LUT_2_2_data = 4003; 244: O_LUT_2_2_data = 4011; 245: O_LUT_2_2_data = 4018;
            246: O_LUT_2_2_data = 4026; 247: O_LUT_2_2_data = 4033; 248: O_LUT_2_2_data = 4041; 249: O_LUT_2_2_data = 4048; 250: O_LUT_2_2_data = 4055; 251: O_LUT_2_2_data = 4063;
            252: O_LUT_2_2_data = 4070; 253: O_LUT_2_2_data = 4077; 254: O_LUT_2_2_data = 4085; 255: O_LUT_2_2_data = 4092;
            default: O_LUT_2_2_data = 4092;
        endcase
    end

endmodule

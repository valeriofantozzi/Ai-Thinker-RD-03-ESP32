# Rd-03D Quick Start Document
**Version:** V1.0.0  
**Copyright © 2023 Shenzhen Ai-Thinker Technology Co., Ltd. All Rights Reserved**

> This is an English transcription of the original Chinese quick start guide for the Ai-Thinker Rd-03D module. Textual content is preserved; images are referenced by captions only.

---

## Table of Contents
1. Flashing Instructions  
2. PC Tool Instructions  
   2.1 PC Connection  
   2.2 Using the PC Tool  
3. UART Communication Protocol  
Contact Us  
Disclaimer and Copyright Notice  
Notes  
Important Statement

---

## 1. Flashing Instructions

**Wiring (Rd-03D ↔ USB-to-TTL):**
- 5V ↔ 5V  
- GND ↔ GND  
- TX ↔ RX (cross)  
- RX ↔ TX (cross)

You can connect via the **right-side pin header** or by **soldering the pads** at the top-left of the module (the original manual shows Figure 1.1 and Figure 1.2).

**Upgrade tool:** `ICLM_Download_JIELI.exe`

**Steps:**
1. Open the firmware upgrade tool.  
2. Click **Refresh Device**, then select the module’s **COM port**.  
3. Set **baud rate = 256000**.  
4. Click **Select File Path** and choose the target **`.ufw`** firmware.  
5. Click **Download** to start upgrading.  
   - The right-side message area shows progress and results.  
   - On success, it will show **“下载成功!” (“Download successful!”)**.  
   - On failure, an error message is displayed.

---

## 2. PC Tool Instructions

### 2.1 PC Connection

**PC tool:** `ICLM_MTT.exe` (portable tool developed for Rd-03D).  
Once the PC and Rd-03D hardware are connected, the tool can **display**, **record**, **save**, and **replay** radar data.

**Steps:**
1. Connect Rd-03D to the USB-to-TTL adapter as in Section 1 (header or solder pads).  
2. Double-click `ICLM_MTT.exe` to open the demo interface.

**UI structure:**
- **Function Button Area (1):**
  - **Detect Device** – checks whether Rd-03D is connected
  - **Start/Stop** – starts or stops receiving radar data
  - **Region Monitoring** – configure monitored polygons and blind zones
  - **Multi-Target / Single-Target** – switch detection mode
  - **Playback / Stop Playback** – replay previously recorded radar data
  - **Data Save / Close Save** – enable/disable data recording
  - **Save Path** – choose where to store recorded data
- **Data Area (2):** real-time distance, angle, and speed of tracked targets
- **Target Display Area (3):** visual radar chart showing target positions in the detection sector

### 2.2 Using the PC Tool

#### 2.2.1 Single-Target / Multi-Target Detection

1. Connect the module and open the PC tool as above.  
2. Click **Detect Device**. If the serial connection is correct, a **“Serial device detected”** dialog appears; click **OK** to continue.  
3. Click **Start/Stop** to begin displaying target positions relative to the radar.  
4. The tool defaults to **Single-Target** mode. Clicking **Multi-Target / Single-Target** switches to **Three-Target Detection** view (i.e., up to 3 targets). Click again to return to Single-Target.  
   - **Note:** Single-Target mode is **not** suitable for tracking multiple targets.

#### 2.2.2 Region Monitoring and Blind-Zone Settings

The tool supports **Region Monitoring** and **Blind-Zone** configuration.

- **Region Monitoring:** define one or more polygonal regions of interest. The region color changes immediately when a human target enters it.  
- **Blind-Zone:** specify near/far distance-gate ranges to ignore (close results and visualization in those gates).

**Operation:**
1. Connect and start detection as in 2.2.1.  
2. Click **Region Monitoring** to open the settings dialog.

**Settings:**
- **Visible Range (distance gates):** default is **0–23** (no blind zone).  
  - You can set the nearest and farthest blind-zone gates, e.g., **1–21** means: blind 1 gate at the near end and blind **(23−21)=2** gates at the far end.  
  - **Distance-gate size:** **36 cm per gate**.  
  - Click **Apply** to update; red areas mark the blind zones.
- **No-Target Color:** background color when no human target is inside the monitored area.  
- **Has-Target Color:** background color when a human target is present.  
- **Add Lighting Area:** start defining a monitored polygon; left-click to add vertices, right-click to end.  
- **Remove All Lighting Areas:** delete all defined monitored polygons.

**Effects:**
- When a human target enters a monitored polygon, the region switches to **Has-Target Color**.  
- When the target leaves, it returns to **No-Target Color**.  
- Multiple monitored polygons can be added by repeating the **Add Lighting Area** process.

#### 2.2.3 Recording and Playback of Radar Data

1. Connect the module and choose the desired detection mode.  
2. When **Start/Stop** shows **Start**, click **Save Path** and choose a directory.  
   - Default save directory: `SaveData` under the PC tool’s folder.  
3. **Data Save** is **off by default**. When the **Data Save** button is enabled (clickable), click it to **enable** recording; click again to **disable**.  
4. With **Data Save** enabled, click **Start** to begin detection; Areas (2) and (3) will show live human target data.  
5. Click **Start/Stop** to stop detection. A data folder will be created at the selected path named by timestamp **`yyyy_mm_dd_hh_mm_ss`**.  
6. Click **Playback / Stop Playback**, select a recorded dataset, and the tool will replay target data into Areas (2) and (3).  
7. Click **Playback / Stop Playback** again to stop replay.

---

## 3. UART Communication Protocol

This protocol is intended for users doing **secondary development** without the PC demo tool.

- **Electrical:** UART (TTL level)  
- **Default baud rate:** **256000**, **1 stop bit**, **no parity**  
- **Modes:** **Single-Target** (default) and **Multi-Target**

**Mode switch commands (hex):**

| Mode          | Command (hex bytes)                                     |
|---------------|----------------------------------------------------------|
| Single-Target | `FD FC FB FA 02 00 80 00 04 03 02 01`                    |
| Multi-Target  | `FD FC FB FA 02 00 90 00 04 03 02 01`                    |

**Uplink frame format (radar → host):**
Header       Frame Data (up to 3 targets)                       Tail
AA FF 03 00  [Target1][Target2][Target3]                        55 CC

**Target data structure (per target):**

| Field              | Type          | Sign convention                                           | Unit  | Notes                                      |
|--------------------|---------------|-----------------------------------------------------------|-------|--------------------------------------------|
| X coordinate       | int16 (signed)| MSB 1 = positive; MSB 0 = negative; remaining 15 bits = | mm    | Axis per Figure 3-1 (recommended mount).   |
|                    |               | absolute value                                            |       |                                            |
| Y coordinate       | int16 (signed)| MSB 1 = positive; MSB 0 = negative; remaining 15 bits = | mm    |                                            |
|                    |               | absolute value                                            |       |                                            |
| Velocity           | int16 (signed)| MSB 1 = positive (receding), MSB 0 = negative (approach) | cm/s  | “Approaching the radar” is negative.       |
| Pixel distance val | uint16        | —                                                         | mm    | Single pixel/bin distance value (raw).     |

**Coordinate system:** The positive directions of the X and Y axes are shown by arrows in the recommended installation diagram (Figure 3-1 in the original).

**Example frame (hex):**
AA FF 03 00 0E 03 B1 86 10 00 4A 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 55 CC

- This indicates only **Target 1** exists (Target 2 and 3 fields are all `0x00`).

**Decoding Target 1:**
- **X:** `0x0E + 0x03*256 = 782`; MSB=0 ⇒ **−782 mm**  
- **Y:** `0xB1 + 0x86*256 = 34481`; `34481 − 2^15 = 1713` ⇒ **+1713 mm**  
- **Velocity:** `0x10 + 0x00*256 = 16`; MSB=0 ⇒ **−16 cm/s** (approaching)  
- **Pixel distance value:** `0x4A + 0x01*256 = 330 mm`

---

## Contact Us

- **Website:** https://www.ai-thinker.com  
- **Forum:** http://bbs.ai-thinker.com  
- **Tmall Store:** https://aithinker.tmall.com  
- **Taobao Store:** https://anxinke.taobao.com/  
- **Docs:** https://docs.ai-thinker.com  
- **LinkedIn:** https://www.linkedin.com/company/ai-thinker  
- **Alibaba (International):** https://ai-thinker.en.alibaba.com

**Email:**
- Technical Support: `support@aithinker.com`  
- Domestic Sales: `sales@aithinker.com`  
- Overseas Sales: `overseas@aithinker.com`

**Phone:** +86-755-29162996  
**Address:** Building C, Huafeng Zhihui Innovation Park, Gushu, Xixiang, Bao’an, Shenzhen. Rooms 403, 408–410.

---

## Disclaimer and Copyright Notice

Information in this document (including URLs) may change without notice.  
This document is provided “as is” without warranties, including merchantability, fitness for a particular purpose, or non-infringement, and without warranties related to any proposals, specifications, or samples referenced elsewhere. Ai-Thinker assumes no liability, including for patent infringement arising from the use of information herein. No license to any intellectual property rights is granted by estoppel or otherwise.  
All test data cited are from Ai-Thinker labs; actual results may vary. All trademarks are the property of their respective owners. Final interpretation belongs to Shenzhen Ai-Thinker Technology Co., Ltd.

---

## Notes

Due to product version upgrades or other reasons, contents of this manual may change. Ai-Thinker reserves the right to modify this manual without notice. This manual is for guidance; Ai-Thinker strives for accuracy but does not guarantee it is error-free, and statements herein do not constitute any express or implied warranty.

---

## Important Statement

Ai-Thinker provides technical/reliability data (including datasheets), design resources (including reference designs), application/design advice, online tools, safety information, and other resources (“Resources”) **as-is**, without defects-free warranty or any express/implied warranties, including fitness for a particular purpose or non-infringement. Ai-Thinker disclaims liability for losses arising from applications or use of its products/circuits.  
Ai-Thinker may change published information (including specifications and product descriptions) without notice; this document supersedes prior versions of the same document number.  
Resources are for skilled developers using Ai-Thinker products. You are fully responsible for: (1) selecting appropriate products; (2) design/verification/operation over the entire lifecycle; (3) ensuring compliance with applicable standards, safety, security, regulatory and other requirements.  
You are authorized to use these Resources only to develop applications for Ai-Thinker products described by the Resources. Without written permission, no entity or individual may excerpt/copy these Resources in whole or in part, nor distribute them in any form. No rights to any other Ai-Thinker or third-party IP are granted. You shall fully indemnify Ai-Thinker and its representatives from claims/damages/costs/losses/liabilities arising from your use of the Resources.  
Ai-Thinker products are provided under Ai-Thinker’s sales terms or other applicable terms accompanying the products. Provision of Resources does not expand or otherwise modify product warranties or disclaimers.
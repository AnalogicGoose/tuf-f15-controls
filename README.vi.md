# ga403wm-hackintosh (Tiếng Việt)

**macOS trên ASUS ROG Zephyrus G14 (GA403WM, 2025): Ryzen AI 9 HX 370 "Strix Point" + RTX 5060 Laptop.**
EFI OpenCore, kext, app thanh menu và ghi chú nghiên cứu. English: [README.md](README.md).

<p align="center">
  <img src="docs/images/hero.jpg" alt="macOS Sequoia trên ROG Zephyrus G14 (GA403WM): RogControl và About This Mac" width="720">
</p>

> ## ⚠️ Dự án "vibe-code". Đọc kỹ trước khi đụng vào máy.
>
> **Gần như toàn bộ mã và tài liệu ở đây do AI (Claude của Anthropic) viết**, do một người chỉ đạo và test trên **một** chiếc
> GA403WM. Chưa ai audit mã. Cụ thể:
>
> * Các kext (`itlmt7925`, `AsusWmi`, `AmdCppc`, `MtkBtFw`, ...) chạy **trong nhân macOS**. Lỗi = kernel panic, treo máy, tệ nhất là hỏng dữ liệu.
>   `AsusWmi` và `AmdCppc` ghi vào EC, BIOS (qua SMI) và thanh ghi CPU.
> * Driver Wi-Fi viết từ đầu và lưu mật khẩu Wi-Fi của bạn **dạng chữ thường** trong kext.
> * EFI và boot-args lấy từ dự án khác rồi chỉnh cho đến khi máy này boot được. Không được "gia cố" (SIP gần như tắt, AMFI nới lỏng, Secure Boot tắt).
> * Mọi thứ chạy được **trên máy tham chiếu**. Máy của bạn (BIOS khác, panel khác, SKU khác) có thể không chạy.
> * **Không bảo hành.** Brick máy, panic, mất dữ liệu, mất bảo hành là việc của bạn. Xem [LICENSE](LICENSE).
>
> Backup tất cả. Giữ USB cứu hộ Windows/Linux. Đừng cài lên máy không thể cài lại. Đừng dùng cho công việc không được phép mất.
> Hãy coi đây là dự án nghiên cứu, không phải sản phẩm.

## Tình trạng trong một phút

| | |
|---|---|
| macOS hỗ trợ | **macOS 15 Sequoia** duy nhất (phát triển trên 15.6.1, đang dùng hằng ngày trên **15.8.1**, build 24H32) |
| **macOS 26 Tahoe** | **Chưa hỗ trợ, chưa test.** Ở lại 15 cho đến khi 15 ổn định. Driver NVIDIA phụ thuộc chỉ dành cho macOS 15. Đừng nâng cấp. |
| Máy | ASUS ROG Zephyrus G14 **GA403WM**, BIOS GA403WM.310, OLED 2880x1800. Biến thể GA403 khác: chưa test |
| Boot | OpenCore 1.0.9 + bản vá Zen 5, từ ESP trong máy; Windows/Linux vẫn nằm trong picker OpenCore |
| Đồ họa | RTX 5060 qua [driver NullMoth](https://github.com/nullmoth/nvidia-macos-driver); **bắt buộc chế độ GPU "Ultimate" (MUX)**, iGPU không dùng được |
| Wi-Fi | Driver tự viết (`itlmt7925`, gọi là "legacy"), đạt tốc độ tối đa của link (HE, 160 MHz). Hiện là **Ethernet** (en*). Phải **inject SSID/mật khẩu** ([hướng dẫn](docs/INSTALL.vi.md#4-wi-fi-inject-mạng)). Driver **native** mới chỉ là bản thử nghiệm dang dở, **chưa chạy**: đừng bật ([dùng driver nào](docs/WIFI-DRIVERS.md#tiếng-việt)) |
| Vấn đề còn lại | Các lỗi lớn trước đây (touchpad, chuột BLE, tốc độ Wi-Fi, độ sáng, điều khiển ROG, HiDPI) đã **sửa xong** tính đến 2026-10-12. Còn: Wi-Fi chưa native (hiện là Ethernet); **sleep/wake chưa chạy ổn (không có deep sleep)**; phần lớn phím Fn chưa map; chưa kiểm chứng âm thanh, đầu đọc SD, % pin |
| Dùng hằng ngày | Ổn khi **cắm sạc và dùng màn hình ngoài qua HDMI**. Thời lượng pin **chưa test, và vì RTX 5060 là GPU duy nhất nên không hứa hẹn gì** về pin |

Chú giải dùng xuyên suốt: ✅ đã kiểm chứng trên máy tham chiếu · 🟡 đã viết, cần test thêm · 🔧 đang làm ·
❌ không chạy · ⚪ chưa làm / chưa kiểm chứng.

## Cái gì chạy

| Mảng | Trạng thái | Ghi chú | Chi tiết |
|---|---|---|---|
| Boot macOS 15 từ ổ trong | ✅ | OpenCore 1.0.9 + `fix-zen5-fsb.diff` (timer chạy sớm ~10 lần trên Zen 5) | [efi/README.md](efi/README.md) |
| CPU (12 nhân / 24 luồng) | ✅ | `AmdCppc` + `AmdCppcPolicy` bật CPPC; nhân Zen 5 lên ~4,9 GHz khi tải (trước đó kẹt 2 GHz) | [docs/CPU-POWER.md](docs/CPU-POWER.md) |
| NVIDIA RTX 5060 + OLED 2880x1800 @ 120 Hz | ✅ | Driver bên thứ ba NullMoth 1.14.2 + hai bản vá cục bộ. Cần MUX = chỉ dGPU. Màn ngoài chạy được | [docs/NULLMOTH.md](docs/NULLMOTH.md) |
| Phím / chuột USB / webcam | ✅ | `ItlXhciKick` sửa lỗi nhả phím trễ 2 giây; port map USB nằm trong EFI chuẩn (sắp có, xem mục Cài đặt) | [docs/USB-XHCI-LAG.md](docs/USB-XHCI-LAG.md) |
| Wi-Fi MT7925 | ✅ | `itlmt7925`: WPA2-PSK, DHCP, HE / 160 MHz đạt tốc độ tối đa của link. Hiện là Ethernet, chưa WPA3 / 6 GHz. **Đang port Wi-Fi native, chưa dùng được** | [docs/WIFI.md](docs/WIFI.md), [dùng driver nào](docs/WIFI-DRIVERS.md#tiếng-việt) |
| Bluetooth MT7925 | ✅ | `MtkBtFw` nạp firmware; tai nghe (classic) và chuột BLE kết nối được (`BtLeDlFix`) | [docs/BLUETOOTH.md](docs/BLUETOOTH.md) |
| Chế độ hiệu năng ROG, quạt, giới hạn sạc | ✅ | `AsusWmi` + app RogControl. Silent/Performance/Turbo đổi xung nhịp và tốc độ quạt thật | [docs/ASUSCTL-MACOS.md](docs/ASUSCTL-MACOS.md) |
| Đèn bàn phím, đèn Slash | ✅ | Qua `AsusWmi` (HID report gửi từ kernel) | [docs/ASUSCTL-MACOS.md](docs/ASUSCTL-MACOS.md) |
| Độ sáng OLED | ✅ | Panel chỉ điều khiển qua AUX; `NVRMEdpBL` (DPCD) điều khiển nó. Thanh trượt và phím độ sáng làm tối chính OLED | [docs/OLED-BRIGHTNESS-DPCD.md](docs/OLED-BRIGHTNESS-DPCD.md) |
| Sleep / wake | ❌ | Chưa có deep sleep; wake chậm và không ổn định. Một chu kỳ hoàn tất thì khôi phục được chính sách CPU, nhưng đừng trông cậy vào sleep hay đóng nắp | [docs/ROADMAP.md](docs/ROADMAP.md) |
| Dual boot Windows / Linux | ✅ | Chọn từ picker OpenCore; có ghi chú sửa entry UEFI bị mất | [docs/DUAL-BOOT.md](docs/DUAL-BOOT.md) |
| Touchpad (I2C `093a:3011`) | ✅ | Hoạt động (chết ngày 2026-10-10, đã sửa 2026-10-12) | [HANDOFF](docs/HANDOFF.md) |
| HiDPI **2x** (như scale 200 % của Windows) | ✅ | "looks like 1440 x 900" trên OLED 2880x1800 ở 120 Hz nhờ file override màn hình. Hỗ trợ tỉ lệ 2x; không hỗ trợ tỉ lệ lẻ | [docs/SCRIPTS.vi.md §4](docs/SCRIPTS.vi.md#4-hidpi-looks-like-1440-x-900-từng-bước) |
| Phím Fn khác ngoài độ sáng / đèn bàn phím | ⚪ | Chưa map; sẽ lập bảng mã phím | [docs/ROADMAP.md](docs/ROADMAP.md) |
| Âm thanh, đầu đọc thẻ SD, % pin | ⚪ | Kext đã có trong EFI (AppleALC layout 33, Sinetek-rtsx, SMCBatteryManager) nhưng **chưa kiểm chứng** | |
| Wi-Fi gốc trong System Settings / AirDrop / Continuity | ⚪ | Cần port `IO80211Controller`; mới ở mức nghiên cứu | [docs/WIFI-AS-WIFI.md](docs/WIFI-AS-WIFI.md) |
| iGPU Radeon 890M | ❌ | Cố ý ẩn khỏi macOS; không có driver | |

Mọi dòng trong bảng đều có ngày tháng và bằng chứng trong [docs/HANDOFF.md](docs/HANDOFF.md) (sổ nhật ký, tiếng Anh).

## Ảnh chụp

Ảnh thật của RogControl và About This Mac (chụp trên GA403WM tham chiếu, macOS 15.8.1). Menu ItlWiFi và picker OpenCore vẫn là ảnh giữ chỗ: bản Wi-Fi native đang port, còn picker cần chụp bằng điện thoại. Xem [docs/images/README.md](docs/images/README.md).

| Popover RogControl | Cửa sổ RogControl | Menu ItlWiFi |
|---|---|---|
| ![RogControl popover](docs/images/rogcontrol-popover.png) | ![RogControl window](docs/images/rogcontrol-window.png) | ![ItlWiFi (giữ chỗ)](docs/images/itlwifi.svg) |

| Picker OpenCore | About This Mac | Đường cong quạt |
|---|---|---|
| ![OpenCore picker (giữ chỗ)](docs/images/opencore-picker.svg) | ![About This Mac](docs/images/about-this-mac.png) | ![Fan curves](docs/images/fan-curves.png) |

## Cài đặt

> 📦 **EFI chuẩn: sắp có.** EFI hoàn chỉnh đang chạy của tác giả (kèm USB port map thật và các thiết lập đã test) sẽ được **upload sau
> thành asset của GitHub release**, README này sẽ được cập nhật khi có. Trước đó, thư mục `efi/` và `EFI-reference.zip` trong release chỉ là
> bản *tham khảo đã làm sạch* (SMBIOS giữ chỗ, USB map giữ chỗ, boot-args debug): đủ để tự ghép EFI theo hướng dẫn, nhưng không phải bản đã test.

Hướng dẫn đầy đủ: **[docs/INSTALL.vi.md](docs/INSTALL.vi.md)** (English: [docs/INSTALL.md](docs/INSTALL.md)). Tóm tắt:

1. BIOS: MUX = **Ultimate**, Above 4G Decoding **ON**, CSM **OFF**, Secure Boot **OFF**. Backup ổ đĩa.
2. Tải **[release v1 build sẵn](https://github.com/thanhnndev/ga403wm-hackintosh/releases)** (kext, app, daemon CPU, OpenCore đã vá, EFI tham khảo, checksum; hoặc
   `bash scripts/build.sh` trên Mac) cộng các kext bên thứ ba trong [efi/KEXTS.md](efi/KEXTS.md). Cách dùng release và mọi script:
   **[docs/SCRIPTS.vi.md](docs/SCRIPTS.vi.md)** (English: [SCRIPTS.md](docs/SCRIPTS.md)).
3. Ghép EFI từ [efi/](efi/README.md), **tự tạo SMBIOS**, thêm OpenCore đã vá.
4. **Inject mạng Wi-Fi** vào `itlmt7925.kext` (chưa có Wi-Fi native dùng được: [dùng driver nào](docs/WIFI-DRIVERS.md#tiếng-việt)) hoặc dùng USB tethering.
5. Cài macOS 15 (nên dùng bộ cài offline: bộ cài online nặng 16 GB nhưng đường Wi-Fi không dành cho việc đó), rồi cài driver NVIDIA, các app và daemon CPU.

## Thành phần

Từng kext, app: làm gì, đến đâu rồi: **[docs/COMPONENTS.md](docs/COMPONENTS.md)** (tiếng Anh).

| Thành phần | Một dòng | Trạng thái |
|---|---|---|
| `itlmt7925.kext` | Driver Wi-Fi MT7925 | ✅ tốc độ tối đa của link, hiện là Ethernet (đang port native) |
| `ItlXhciKick.kext` | Sửa lỗi USB trễ trên xHCI AMD | ✅ |
| `AsusWmi.kext` | ASUS WMI/EC/HID: profile, quạt, giới hạn sạc, đèn nền, Slash | ✅ |
| `AmdCppc.kext` + `AmdCppcPolicy` | Điều khiển boost/EPP CPU + daemon | ✅ |
| `MtkBtFw.kext` | Nạp firmware Bluetooth MT7925 | ✅ |
| `BtLeDlFix.kext` | Plugin Lilu cho chuột BLE | ✅ |
| `NVRMEdpBL.kext` (trong fork NullMoth) | Độ sáng OLED qua eDP AUX | ✅ (PR upstream đang chờ) |
| `ItlDiag.kext` | chẩn đoán lúc boot | công cụ debug |
| **ROG Control** (`RogControl.app`) | thanh menu + cửa sổ: profile, quạt, sạc, đèn | ✅ |
| **ItlWiFi** (`ItlWiFi.app`) | Wi-Fi trên thanh menu: quét, kết nối, speed test | ✅ |
| **TapGuard** (`TapGuard.app`) | bỏ qua tap touchpad khi đang gõ | ✅ |

### ROG Control làm được gì

* Chọn chế độ **Silent / Performance / Turbo / Manual** (Turbo tắt khi dùng pin, như Armoury Crate); Manual chỉnh SPL / SPPT / FPPT.
* Nhiệt độ CPU (Tctl), công suất package (RAPL), tốc độ quạt CPU/GPU, **đường cong quạt** của firmware (biểu đồ CPU/GPU).
* **Giới hạn sạc** 60 / 80 / 100 %, thông tin pin; cài đặt riêng cho AC và pin, tự áp lại khi đổi nguồn và sau khi đăng nhập.
* **Đèn bàn phím** và **đèn Slash** (độ sáng, chế độ), gửi lại sau khi wake.
* Trạng thái GPU MUX (chỉ đọc), `--bench` đo hiệu năng từng profile.
* Chưa có: đổi GPU MUX (cố ý chỉ đọc), hiệu ứng Aura từng phím, overdrive panel (BIOS này không có).

## Roadmap

**[docs/ROADMAP.md](docs/ROADMAP.md)** liệt kê từng hạng mục: đang làm gì, ở nhánh nào, bước kế tiếp. Điểm chính:

* 🔧 **Wi-Fi native** (port `IO80211Controller`, đang làm) để menu bar và System Settings hoạt động; driver `itlmt7925` vẫn giữ làm phương án dự phòng
* 🔧 mượt hơn cho desktop (WindowServer CPU), đẩy các bản vá NVIDIA (VRAM clear, độ sáng OLED) lên upstream
* ❌ **sửa sleep/wake** (hiện chưa có deep sleep), ⚪ **map các phím Fn** của bàn phím, ⚪ test thời lượng pin (không hứa hẹn: dGPU là GPU duy nhất)
* ⚪ kiểm chứng âm thanh, đầu đọc SD, % pin; một chủ máy thứ hai cài lại được từ repo
* ⚪ bản release đầu tiên có kext build bằng CI (bước build app trong CI hiện đang lỗi: xem roadmap)
* ⚪ **Chuẩn hoá codebase** (hàm dùng chung, dọn code debug, script): từng thành phần một, chỉ sau khi thành phần đó chứng minh ổn định
* ⚪ **macOS 26**: chỉ sau khi 15 ổn định. Chưa lên kế hoạch.

## Cấu trúc repo

```
efi/                 config.plist OpenCore đã làm sạch + SSDT, danh sách kext bên thứ ba (KEXTS.md, kexts.json)
kexts/               itlmt7925, ItlXhciKick, AsusWmi, AmdCppc, MtkBtFw, BtLeDlFix, ItlDiag
macos/               RogControl, ItlWiFi, TapGuard (app Swift), AmdCppcPolicy (launch daemon)
fw/                  firmware MediaTek MT7925 (linux-firmware) kèm license
patches/opencore/    bản vá OpenCorePkg cho Zen 5
scripts/             build, cài đặt, inject Wi-Fi, thu log, export EFI + kiểm tra riêng tư
tools/               công cụ đọc kernel collection, thăm dò HiDPI / frame pacing / xung CPU
test/                test trên host (WPA2, capabilities, Block Ack, ...), chạy trong CI
docs/                hướng dẫn cài, hướng dẫn script (có HiDPI), thành phần, roadmap, ghi chú nghiên cứu, HANDOFF (sổ nhật ký)
```

## Riêng tư

* Repo không chứa mật khẩu Wi-Fi hay định danh SMBIOS: `Info.plist` để trống `itl_ssid*/itl_psk*`, `efi/OC/config.plist` chỉ có
  giá trị giữ chỗ `CHANGE_ME_*`, CI sẽ fail nếu lộ giá trị thật. Kext **bạn** đã inject mật khẩu thì chứa mật khẩu dạng chữ thường: đừng chia sẻ.
* Log chứa địa chỉ MAC của card và SSID/BSSID xung quanh. Che trước khi đăng issue.

## Đóng góp, ghi công, giấy phép

* **Tác giả và người duy trì:** [thanhnndev](https://github.com/thanhnndev) (<https://github.com/thanhnndev>): bring-up GA403WM,
  driver Wi-Fi `itlmt7925`, RogControl và phần còn lại của repo này.
* Báo cáo từ các chủ GA403WM khác là thứ giá trị nhất: mở issue bằng mẫu **Hardware report** và đọc [CONTRIBUTING.md](CONTRIBUTING.md).
* Dự án đứng trên công sức của người khác: [NullMoth](https://github.com/nullmoth/1401) (nền EFI và driver NVIDIA), Acidanthera
  (OpenCore, Lilu, VirtualSMC, AppleALC, ...), các dự án Linux `mt76`, `btmtk`, `asus-wmi`, `asusctl` (tham khảo thiết kế). Danh sách và giấy phép: [NOTICE.md](NOTICE.md).
* Mã: [Clear BSD License](LICENSE) (dựa trên driver Linux `mt76`, BSD-3-Clause-Clear, © MediaTek). Firmware trong `fw/`: [license của MediaTek](fw/LICENCE.mediatek).
  `efi/` dẫn xuất từ `nullmoth/1401` (PolyForm Noncommercial 1.0.0), **không** thuộc Clear BSD: chỉ dùng phi thương mại.
* Không liên kết với ASUS, Apple, NVIDIA, MediaTek, AMD hay NullMoth. Bạn tự chịu trách nhiệm tuân thủ điều khoản giấy phép macOS của Apple.

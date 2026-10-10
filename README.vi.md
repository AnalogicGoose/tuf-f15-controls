# itlmt7925 (Tiếng Việt)

Các thành phần giúp chạy macOS trên **ASUS ROG Zephyrus G14 (GA403WM, Ryzen AI 9 HX 370)** và các laptop AMD Strix Point có
card Wi-Fi **MediaTek MT7925**, làm trong quá trình dựng Hackintosh bằng OpenCore.

| Thành phần | Chức năng | Trạng thái |
|---|---|---|
| `kexts/itlmt7925` | driver Wi-Fi MT7925: nạp firmware, quét, WPA2-PSK, giao diện Ethernet | chạy được trong Recovery, tối giản, tốc độ truyền cố định 6 Mb/s |
| `kexts/ItlXhciKick` | sửa lỗi bàn phím/chuột USB trễ ~2 giây (gõ "d" ra "ddddd") trên xHCI AMD chỉ có 1 vector MSI | chạy tốt, xem `docs/USB-XHCI-LAG.md` |
| `kexts/ItlDiag` | chẩn đoán lúc boot (đọc log qua NVRAM) | công cụ debug |
| `efi/` | `config.plist` + SSDT đang dùng, đã xoá serial/MLB/UUID/ROM | tham khảo |
| `patches/opencore` | vá OcCpuLib cho Zen 5 (family 1Ah) | dùng trong EFI tham khảo |

> **Thử nghiệm.** Mọi thứ chỉ được kiểm chứng trên một máy, trong môi trường macOS 15.6.1 Recovery boot từ USB. Chưa thử cài
> macOS đầy đủ. Driver Wi-Fi không có giao diện, không roaming, không tự kết nối lại. Hãy backup EFI và thử từ USB trước.

## Dùng nhanh

1. Lấy kext: Release (khi có), artifact của workflow `build-kexts`, hoặc `bash scripts/build.sh` trên Mac.
2. Ghép EFI từ `efi/` (xem `efi/README.md`, `efi/KEXTS.md`). **Tự tạo SMBIOS** (serial, MLB, UUID, ROM) — file trong repo chỉ chứa
   giá trị giữ chỗ `CHANGE_ME_*`.
3. Thêm mạng Wi-Fi vào kext (Windows, PowerShell quyền admin):
   `.\scripts\inject-creds.ps1 -Kext <EFI>\OC\Kexts\itlmt7925.kext -Ssid 'TenWifi'` (tối đa 4 mạng, thứ tự = ưu tiên).
   Mật khẩu nằm **dạng chữ thường** trong kext trên USB — đừng chia sẻ USB hay file đó.
4. boot-args: `itlmt_m1=75 itlmt_m2=1 itlmt_m3=1`.
5. Boot, chờ 2–3 phút, đọc log bằng `scripts/read-nvram.ps1` (xem `docs/DEBUGGING.md`). Log chứa MAC và tên các mạng xung quanh —
   hãy che trước khi đăng.

Tài liệu chi tiết (tiếng Anh): `docs/WIFI.md`, `docs/USB-XHCI-LAG.md`, `docs/DEBUGGING.md`, `docs/ROADMAP.md`.

## Việc còn lại

* **USB mapping chưa làm** (đang dùng `UTBDefault.kext` làm chỗ giữ chỗ).
* Rate control / tăng tốc độ Wi-Fi, tự kết nối lại, tiết kiệm điện, WPA3, 6 GHz.
* Thử trên bản cài macOS đầy đủ.

## Giấy phép

[Clear BSD License](LICENSE) cho mã trong repo (dựa trên driver Linux `mt76`, © MediaTek, BSD-3-Clause-Clear).
Firmware trong `fw/` là của MediaTek, giữ nguyên [license riêng](fw/LICENCE.mediatek). Xem `NOTICE.md`.

# ga403wm-hackintosh (Tiếng Việt)

**EFI OpenCore, kext và ghi chú để chạy macOS trên ASUS ROG Zephyrus G14 (GA403WM), Ryzen AI 9 HX 370 (Strix Point, Zen 5).**
Gồm driver Wi-Fi MediaTek MT7925 viết từ đầu và bản sửa lỗi bàn phím/chuột USB bị trễ trên xHCI của AMD Strix Point.

> **Thử nghiệm, chỉ một máy.** Mọi thứ chỉ được kiểm chứng trên một chiếc GA403WM, boot macOS 15.6.1 Recovery từ USB với
> OpenCore 1.0.9. Chưa cài macOS đầy đủ lên ổ trong. Hãy backup, thử từ USB trước, và đọc `docs/DUAL-BOOT.md` nếu máy còn
> Windows/Linux.

| Thành phần | Chức năng | Trạng thái |
|---|---|---|
| `efi/` | `config.plist` + SSDT đang dùng, đã thay serial/MLB/UUID/ROM bằng giá trị giữ chỗ; danh sách kext bên thứ ba | tham khảo |
| `kexts/itlmt7925` | driver Wi-Fi MT7925: nạp firmware, quét, WPA2-PSK, giao diện Ethernet | chạy được trong Recovery, tối giản, TX cố định 6 Mb/s |
| `kexts/ItlXhciKick` | sửa lỗi USB trễ ~2 giây (gõ "d" ra "ddddd") do xHCI AMD chỉ có 1 vector MSI | chạy tốt, xem `docs/USB-XHCI-LAG.md` |
| `kexts/ItlDiag` | chẩn đoán lúc boot, ghi log qua NVRAM | công cụ debug |
| `patches/opencore` | vá OcCpuLib cho Zen 5 (family 1Ah): timer chạy sớm ~10 lần | dùng trong EFI tham khảo |
| dGPU RTX 5060 | dùng [driver NVIDIA của NullMoth](https://github.com/nullmoth/nvidia-macos-driver) (cài *trong* macOS 15, không nằm trong EFI); `config.plist` đã có sẵn boot-args và SIP của nó, repo này không kiểm chứng | xem `efi/README.md` |
| `scripts/restore-limine-entry.ps1` | tạo lại entry UEFI của Limine/Linux khi NVRAM bị mất | xem `docs/DUAL-BOOT.md` |

Tên các kext (`itlmt7925`, `ItlXhciKick`, `ItlDiag`) giữ nguyên vì dự án bắt đầu từ driver Wi-Fi.

## Dùng nhanh

1. Lấy kext: Release (khi có), artifact của workflow `build-kexts`, hoặc `bash scripts/build.sh` trên Mac.
2. Ghép EFI từ `efi/` (xem `efi/README.md`, `efi/KEXTS.md`). **Tự tạo SMBIOS** (serial, MLB, UUID, ROM) — file trong repo chỉ chứa
   giá trị giữ chỗ `CHANGE_ME_*`.
3. Thêm mạng Wi-Fi vào kext (Windows, PowerShell quyền admin):
   `.\scripts\inject-creds.ps1 -Kext <EFI>\OC\Kexts\itlmt7925.kext -Ssid 'TenWifi'` (tối đa 4 mạng, thứ tự = ưu tiên).
   Mật khẩu nằm **dạng chữ thường** trong kext trên USB — đừng chia sẻ USB hay file đó.
4. boot-args: `itlmt_m1=0 itlmt_m2=1 itlmt_m3=1`.
5. Boot, chờ 2–3 phút, đọc log bằng `scripts/read-nvram.ps1` (xem `docs/DEBUGGING.md`). Log chứa MAC và tên các mạng xung quanh —
   hãy che trước khi đăng.

Tài liệu chi tiết (tiếng Anh): `efi/README.md`, `docs/WIFI.md`, `docs/USB-XHCI-LAG.md`, `docs/DUAL-BOOT.md`, `docs/DEBUGGING.md`, `docs/ROADMAP.md`.

## Việc còn lại

* **USB mapping chưa làm** (đang dùng `UTBDefault.kext` làm chỗ giữ chỗ).
* Rate control / tăng tốc độ Wi-Fi, tự kết nối lại, tiết kiệm điện, WPA3, 6 GHz.
* Thử trên bản cài macOS đầy đủ.

## Giấy phép

[Clear BSD License](LICENSE) cho mã trong repo (dựa trên driver Linux `mt76`, © MediaTek, BSD-3-Clause-Clear).
Firmware trong `fw/` là của MediaTek, giữ nguyên [license riêng](fw/LICENCE.mediatek). Thư mục `efi/` dẫn xuất từ [nullmoth/1401](https://github.com/nullmoth/1401) (PolyForm Noncommercial 1.0.0), **không** thuộc Clear BSD. Xem `NOTICE.md`.

# Moonlight Extended

Moonlight Extended is an experimental Windows build of [Moonlight PC](https://github.com/moonlight-stream/moonlight-qt) with native PlayStation 5 DualSense controller support over both **USB and Bluetooth**.

It is designed to work with [Apollo Extended](https://github.com/Taveszfito/Apollo-Extended), enabling native DualSense emulation on the host while preserving controller features that standard gamepad emulation cannot carry end to end.

---

## 🎮 Native DualSense Support

Use a DualSense directly on the Windows client without converting it to an Xbox or DualShock 4 controller first.

Currently supported features include:

* Buttons, sticks, triggers, touchpad, and motion input
* Adaptive triggers
* Standard rumble and HD haptics
* Controller speaker audio
* DualSense built-in microphone forwarding to the host
* Global microphone mute using the DualSense microphone button, with microphone LED and on-screen status feedback
* Automatic game audio switching to headphones connected through the DualSense 3.5 mm jack
* Light bar, player LEDs, and microphone LED control
* Wired USB and wireless Bluetooth operation
* Selectable host controller emulation
* Selectable DualSense audio and haptics mode

> **Apollo Extended required:** Native DualSense host emulation, HD haptics, controller-speaker audio, microphone forwarding, and other Extended controller options require a compatible [Apollo Extended](https://github.com/Taveszfito/Apollo-Extended) host. Standard Sunshine and Apollo hosts remain usable for regular Moonlight streaming, but they cannot provide the complete Extended DualSense feature set.

---

## 🎙️ Microphone Forwarding

Moonlight Extended adds client-to-host microphone forwarding, allowing voice chat and other microphone input to be used during a remote streaming session.

Any microphone available on the client PC can be selected and forwarded to the host, including the DualSense built-in microphone when a controller is connected.

Microphone transmission can be muted directly on the client, preventing microphone audio from being sent to the host.

Microphone forwarding uses the **Steam Streaming audio drivers** as its backend, so **Steam must be installed on the host PC** for microphone forwarding to work.

> **Apollo Extended required:** Microphone forwarding is an Extended protocol feature and requires a compatible [Apollo Extended](https://github.com/Taveszfito/Apollo-Extended) host. Standard Sunshine and Apollo hosts do not support microphone forwarding.

---

## ⚠️ Experimental Test Build

Moonlight Extended is currently an **experimental test build** and remains under active development. Use it at your own risk.

* Fully stable operation is not guaranteed across every hardware and software configuration.
* Back up important settings before installation and report reproducible issues with logs and hardware details.
* A clean installation is recommended when replacing an earlier development build.

### Windows Defender and SmartScreen

Early test installers may trigger Microsoft Defender or SmartScreen because they are new and may not yet have an established reputation or commercial code-signing certificate.

If the installation fails or Windows blocks the installer, temporarily disable Microsoft Defender and install Moonlight Extended.

---

## 📥 Downloads

Download the latest Windows installer from this repository's [Releases](https://github.com/Taveszfito/moonlight-extended/releases) page.

The matching host is available from the [Apollo Extended releases](https://github.com/Taveszfito/Apollo-Extended/releases) page.

---

## Credits

Moonlight Extended is based on [Moonlight PC](https://github.com/moonlight-stream/moonlight-qt).

DualSense support builds on [SDL](https://github.com/libsdl-org/SDL), with Extended changes maintained in the project's [SDL fork](https://github.com/Taveszfito/SDL). Streaming protocol changes are maintained in [moonlight-common-c](https://github.com/Taveszfito/moonlight-common-c).

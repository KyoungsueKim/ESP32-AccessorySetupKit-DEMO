# ESP32 AccessorySetupKit DEMO

## ESP32 BLE Dice Accessory with AccessorySetupKit Integration
![image](https://github.com/KyoungsueKim/ESP32-AccessorySetupKit-DEMO/blob/main/image.jpeg)
This repository contains the ESP32 firmware code for a BLE (Bluetooth Low Energy) dice accessory that integrates with
iOS applications using Apple’s AccessorySetupKit, introduced in iOS 18 during WWDC 2024. This project allows you to test
Apple’s sample app with actual hardware, providing a hands-on experience of how AccessorySetupKit facilitates seamless
discovery, pairing, and interaction between iOS devices and BLE accessories.

### Project Overview

The project demonstrates how to create a BLE dice accessory using the ESP32 platform. The accessory emulates a
digital dice that communicates with an iOS app, leveraging the new AccessorySetupKit SDK to enable easy onboarding and
interaction. This setup allows developers to explore the capabilities of AccessorySetupKit with real hardware instead of
simulations.

### Features

- AccessorySetupKit Integration: Utilizes Apple’s AccessorySetupKit to enable seamless discovery, authorization, and
  interaction with iOS devices.
- BLE Communication: Implements BLE services and characteristics to communicate dice roll values to the iOS app.
- Secure Pairing: Initiates secure BLE pairing upon connection, ensuring data privacy and integrity.
- Random Dice Roll Simulation: Simulates dice rolls by generating random values between 1 and 6 at regular intervals.
- Real-time Notifications: Sends dice roll updates to the iOS app using BLE notifications.
- Bonding and Encryption: Supports BLE bonding to remember paired devices and establish encrypted connections.

### Getting Started

#### Prerequisites

- ESP32 Development Board: An ESP32 module or development board.
- ESP-IDF Environment: ESP-IDF version compatible with ESP32 (e.g., v4.4 or later).
- iOS Device: An iOS device running iOS 18 or later.
- Xcode: For building and running the iOS sample app.

#### Installation

1. Clone the Repository

```bash
git clone https://github.com/KyoungsueKim/ESP32-AccessorySetupKit-DEMO.git
```

2. Set Up ESP-IDF
   Follow
   the [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html)
   to set up the development environment for ESP32.
3. Configure the Project
   Navigate to the project directory and run menuconfig to configure the project settings if necessary.

```bash
cd ESP32-AccessorySetupKit-DEMO
idf.py menuconfig
```

4. Build and Flash the Firmware
   Build and flash the firmware to the ESP32 device using the following commands:

```bash
idf.py build flash monitor
```

The monitor command will display logs from the ESP32 device.

### iOS App Integration

iOS App Integration

To interact with the ESP32 BLE dice accessory, use Apple’s sample iOS app provided in the AccessorySetupKit
documentation.

**Using the Sample iOS App**

You can download the iOS sample app source code from Apple’s official documentation:

1. Download the Sample App

  - Download Source
    Code: [Authorizing a Bluetooth Accessory to Share a Dice Roll](https://developer.apple.com/documentation/accessorysetupkit/authorizing-a-bluetooth-accessory-to-share-a-dice-roll)

2. Open the Project in Xcode

  - Unzip the downloaded project.
  - Open the .xcodeproj file in Xcode on your Mac.

3. Run the Two Targets on Separate Devices

  - ASKSampleAccessory Target: Simulates a Bluetooth dice accessory.
  - ASKSample Target: Demonstrates how to use AccessorySetupKit to onboard the accessory.

4. Deploy the Apps

  - Connect your two iOS devices running iOS 18 or later.
  - Build and run the ASKSampleAccessory app on one device.
  - Build and run the ASKSample app on the other device.

5. Using the Applications

  - On the device running ASKSampleAccessory, tap “Power On” to start advertising as a BLE accessory.
  - On the device running ASKSample, tap “Add Dice” and follow the prompts to authorize the accessory using the
    AccessorySetupKit UI.
  - After authorization, the accessory is ready for use in the app.
  - You can now receive dice roll results in ASKSample whenever you roll the dice in ASKSampleAccessory or via the ESP32
    hardware.

### Testing with ESP32 Hardware

By running the ESP32 firmware on your development board, you can replace the simulated accessory (ASKSampleAccessory) with
actual hardware.

- Power On the ESP32 Accessory
    - Ensure the ESP32 device is running the firmware from this repository.
    - The device will start advertising as a BLE dice accessory.
- Discover and Authorize on the iOS App
    - Open ASKSample on your iOS device.
    - Tap “Add Dice” to search for accessories.
    - Select your ESP32 accessory from the list.
    - Follow the AccessorySetupKit UI to authorize and connect.
- Interact with the Accessory
    - Once connected, the app will display real-time dice roll values from the ESP32 accessory.
    - The accessory simulates dice rolls at regular intervals and sends updates to the app.

### Project Structure

- main/: Contains the main application code for the ESP32 firmware.
- main.c: The primary source file implementing BLE services, characteristics, and the dice roll logic.
- CMakeLists.txt: Build configuration files for the project.

### Key Implementation Details

#### BLE Services and Characteristics

- Service UUID: The accessory advertises a custom service UUID to be identified by the iOS app.
- Dice Roll Characteristic: A characteristic with read and notify properties to send dice roll values.
- Characteristic UUID: Defined for the dice roll characteristic (0xFF3F).

#### Security and Pairing

- Immediate Pairing: The accessory initiates BLE pairing immediately upon connection from the iOS device.
- Security Parameters:
    - Authentication Requirement: Secure Connections with bonding (ESP_LE_AUTH_REQ_SC_BOND).
    - IO Capabilities: No input/output capabilities (ESP_IO_CAP_NONE).
- Bonding Information: The accessory stores bonding information to remember paired devices.

#### Dice Roll Logic

- Random Value Generation: Generates a random number between 1 and 6 to simulate a dice roll.
- Periodic Updates: Uses a FreeRTOS task to update the dice value at regular intervals.
- BLE Notifications: Sends the updated dice value to the connected iOS app using notifications.

### Troubleshooting

- Stack Overflow: Ensure that the stack size for tasks is sufficient. Adjust the stack size in xTaskCreate if a stack
  overflow occurs.
- Authentication Errors: Verify that the security parameters match the capabilities of the accessory. Remove MITM
  protection if the accessory lacks input/output capabilities.
- BLE Connection Issues: Make sure that the BLE service and characteristic UUIDs match between the accessory and the iOS
  app.

### Contributing

Contributions are welcome! Please open an issue or submit a pull request for any bugs, enhancements, or feature
requests.

### License

This project is licensed under the GPL-3.0 License - see the LICENSE file for details.

### Acknowledgments

- Espressif Systems: For the ESP32 hardware and ESP-IDF development framework.
- [AccessorySetupKit](https://developer.apple.com/documentation/accessorysetupkit): Enable privacy-preserving discovery
  and configuration of accessories.
- [WWDC 2024 Session 10203](https://developer.apple.com/videos/play/wwdc2024/10203/): Meet AccessorySetupKit.
- Community: Thanks to all the developers and contributors who have provided valuable resources and support.

### Contact

For questions or support, please open an issue or contact zp5njqlfex@gmail.com.

**Disclaimer**: This project is for educational purposes and serves as an example of integrating ESP32 BLE accessories
with iOS apps using AccessorySetupKit. Ensure compliance with all relevant guidelines and regulations when developing
and distributing BLE accessories and iOS applications.

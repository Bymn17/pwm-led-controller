
This project creates a system where three LEDs' brightness levels are controlled based on how quickly two pushbuttons are alternately pressed.
The project consists of:

1-) A Linux kernel module
•	Handles button press interrupts
•	Measures button press speed
•	Controls LED brightness using PWM
•	Provides both character device and sysfs interfaces

2-) Two Rust user applications: 
•	One using the device driver interface
•	One using the sysfs interface

The process begins when the kernel module loads, configuring the GPIO pins, initiating button press monitoring, starting PWM signal generation,
and establishing both device file and sysfs interfaces for communication. Users can interact with the system through either of the
Rust applications(sysfs & device driver). These applications continuously read the current button press speed detected by the kernel module,
then calculate the appropriate duty cycles based on that speed. These calculated values are then sent back to the kernel module,
which adjusts the PWM signals accordingly, resulting in visible changes to the LED brightness that directly correspond to how quickly the buttons
are being pressed. Throughout this entire process, the kernel module's interrupt handlers work in the background, constantly measuring button press
speed and updating internal variables. These updated values remain accessible to both interface types, ensuring that the system always responds
dynamically to user input regardless of which interface is being used to interact with it.

## Circuit Setup
Connect the components as follows:
- LED1: GPIO17 → LED → Resistor → GND
- LED2: GPIO27 → LED → Resistor → GND
- LED3: GPIO22 → LED → Resistor → GND
- Button1: GPIO23 → Button → GND (with pull-down resistor)
- Button2: GPIO24 → Button → GND (with pull-down resistor)

  
## Software Components
1. Kernel Module (`pwm_led_controller.c`): Handles button interrupts, PWM generation, and exposes interfaces
2. Device Driver Client (`device_driver.rs`): Rust application that uses the character device interface
3. Sysfs Client (`sysfs.rs`): Rust application that uses the sysfs interface
4. Makefile: Builds all components

## Building and Installing
1. Clone this repository: git clone https://github.com/Bymn17/pwm-led-controller.git
2. Build all components: make all
3.  Install the kernel module and applications: sudo make install

## Usage

### Using the Device Driver Interface
Run the device driver client: sudo device_driver

### Using the Sysfs Interface
Run the sysfs client: sudo sysfs


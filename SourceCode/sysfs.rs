 
 // This application reads button press speed from sysfs
 // and sets LED duty cycles accordingly.
 

use std::fs::{File, OpenOptions};
use std::io::{Read, Write, Error};
use std::thread::sleep;
use std::time::Duration;

// Constants for sysfs paths and speed mapping
const SYSFS_PATH: &str = "/sys/kernel/pwm_led_controller";  // Base path to sysfs entries
const MAX_SPEED: u64 = 10;  // Max button press speed 
const MIN_SPEED: u64 = 1;   // Min button press speed 

fn main() -> Result<(), Error> {
    println!("Project LED Controller - Sysfs Interface");
    println!("Press Ctrl+C to exit");
    
    // Main loop
    loop {
        // Read current button press speed from sysfs
        let speed = read_speed()?;
        println!("Current button press speed: {} presses/second", speed);
        
        // Map speed to LED duty cycles
        let (led1, led2, led3) = map_speed_to_duty_cycles(speed);
        println!("Setting LED duty cycles: L1={}%, L2={}%, L3={}%", led1, led2, led3);
        
        // Update LED duty cycles
        set_led_duty_cycles(led1, led2, led3)?;
        
        
        sleep(Duration::from_millis(500));
    }
}

// read_speed - Reads the current button press speed from sysfs

fn read_speed() -> Result<u64, Error> {
    // Open sysfs file for button speed
    let mut file = File::open(format!("{}/button_speed", SYSFS_PATH))?;
    let mut buffer = String::new();
    
    // Read content into buffer
    file.read_to_string(&mut buffer)?;
    
    
    Ok(buffer.trim().parse::<u64>().unwrap_or(0))
}

//set_led_duty_cycles - Sets LED duty cycles through sysfs

fn set_led_duty_cycles(led1: u32, led2: u32, led3: u32) -> Result<(), Error> {
    // Set LED1 duty cycle
    let mut file = OpenOptions::new().write(true).open(format!("{}/led1_duty", SYSFS_PATH))?;
    file.write_all(led1.to_string().as_bytes())?;
    
    // Set LED2 duty cycle
    let mut file = OpenOptions::new().write(true).open(format!("{}/led2_duty", SYSFS_PATH))?;
    file.write_all(led2.to_string().as_bytes())?;
    
    // Set LED3 duty cycle
    let mut file = OpenOptions::new().write(true).open(format!("{}/led3_duty", SYSFS_PATH))?;
    file.write_all(led3.to_string().as_bytes())?;
    
    Ok(())
}

// map_speed_to_duty_cycles - Maps button press speed to LED duty cycles
 
fn map_speed_to_duty_cycles(speed: u64) -> (u32, u32, u32) {
    if speed <= MIN_SPEED {
        // Min speed: L1 at minimum 10% L2 and L3 off
        return (10, 0, 0);
    } else if speed >= MAX_SPEED {
        // Max speed: All LEDs at max
        return (100, 100, 100);
    } else {
        // Scale LEDs based on speed
        let range = MAX_SPEED - MIN_SPEED;
        let position = speed - MIN_SPEED;
        let percentage = (position as f64) / (range as f64);
        
        // Calculatea LED duty cycles:
        // LED1: scales from 10% to 100% across the entire range
        let led1 = 10 + (90.0 * percentage) as u32;
        
        // LED2: turns on at 33% of the range, scales to 100%
        let led2 = if percentage > 0.33 { 
            ((percentage - 0.33) * 150.0) as u32 
        } else { 
            0 
        };
        
        // LED3: turns on at 66% of the range, scales to 100%
        let led3 = if percentage > 0.66 { 
            ((percentage - 0.66) * 300.0) as u32 
        } else { 
            0 
        };
        
        // Ensure we're within bounds (0-100%)
        let led2 = led2.min(100);
        let led3 = led3.min(100);
        
        return (led1, led2, led3);
    }
}
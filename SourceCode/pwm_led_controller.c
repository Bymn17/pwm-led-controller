/*
 * PWM LED Controller based on Button Speed
 * 
 * This kernel module implements a system where the brightness of three LEDs is
 * controlled based on how fast two pushbuttons are alternately pressed.
 * The module uses PWM with a 10ms period to control LED intensity.
 *
 */

#include <linux/atomic.h>      
#include <linux/cdev.h>        
#include <linux/delay.h>       
#include <linux/device.h>      
#include <linux/fs.h>         
#include <linux/init.h>       
#include <linux/kernel.h>      
#include <linux/module.h>      
#include <linux/printk.h>      
#include <linux/types.h>       
#include <linux/uaccess.h>     
#include <linux/version.h>     
#include <linux/io.h>          
#include <linux/hrtimer.h>     /* High-resolution timer support */
#include <linux/ktime.h>       
#include <linux/err.h>         
#include <linux/gpio.h>        
#include <linux/interrupt.h>   /* For interrupt handling */
#include <linux/sysfs.h>       
#include <linux/kobject.h>     

/* Module parameters and constants */
#define DEVICE_NAME "pwm_led_controller"   // Name of device in /dev
#define CLASS_NAME "pwm_led_controller"    // Name of device class
#define SUCCESS 0               // Success return code 
#define BUF_LEN 80              // Buffer length for device Input-Output 

// GPIO Pins 
#define LED1_PIN 17  // GPIO pin for LED1 
#define LED2_PIN 27  // GPIO pin for LED2 
#define LED3_PIN 22  // GPIO pin for LED3 
#define BTN1_PIN 23  // GPIO pin for button 1 
#define BTN2_PIN 24  // GPIO pin for button 2 

/* PWM Parameters */
#define PWM_PERIOD_NS 10000000  // 10ms in nanoseconds 
#define MIN_DUTY 0              // 0% duty cycle 
#define MAX_DUTY 100            // 100% duty cycle 

// global variables 
static int major;                   // number assigned to device 
static struct class *projectClass = NULL;    // Device class 
static struct device *projectDevice = NULL;  // Device structure 
static struct kobject *project_kobj;         // Kobject for sysfs entries 

// LED PWM duty cycles (percentage 0-100) 
static int led1_duty = 0; 
static int led2_duty = 0; 
static int led3_duty = 0; 

// Button press timing
static ktime_t last_press_time;         // Time of last button press 
static ktime_t current_press_time;      // Time of current button press 
static int last_button = 0;             // 0 = n/a, 1 = button 1, 2 = button 2 
static int button_press_count = 0;      // Total number of button presses 
static int valid_alternating_count = 0; // Number of valid alternating presses 
static u64 total_press_time = 0;        // Sum of intervals between alternating presses 
static u64 avg_press_interval = 0;      // Average interval in nanoseconds 

// for PWM control 
static struct hrtimer pwm_timer;    // High-resolution timer for PWM
static int pwm_state = 1;           // LED state (1=ON, 0=OFF) 
static ktime_t pwm_on_time;         // Duration for PWM ON state 
static ktime_t pwm_off_time;        // Duration for PWM OFF state 

// for device Input-Output 
static char message[BUF_LEN];       // Buffer for message to user space 
static char *msg_ptr;               // Pointer to current position in message 

// Function prototypes
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char __user *, size_t, loff_t *);
static ssize_t led1_duty_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t led1_duty_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t led2_duty_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t led2_duty_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t led3_duty_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t led3_duty_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t button_speed_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

//file operations for device driver 
static struct file_operations project_fops = {
    .read = device_read,            // Called when device is read from 
    .write = device_write,          // Called when device is written to 
    .open = device_open,            // Called when device is opened 
    .release = device_release,      // Called when device is closed 
};

// Sysfs Definitions 
static struct kobj_attribute led1_attribute = 
    __ATTR(led1_duty, 0664, led1_duty_show, led1_duty_store);  // LED1 duty cycle 
static struct kobj_attribute led2_attribute = 
    __ATTR(led2_duty, 0664, led2_duty_show, led2_duty_store);  // LED2 duty cycle 
static struct kobj_attribute led3_attribute = 
    __ATTR(led3_duty, 0664, led3_duty_show, led3_duty_store);  // LED3 duty cycle 
static struct kobj_attribute speed_attribute = 
    __ATTR(button_speed, 0444, button_speed_show, NULL);       // Button speed 

// Grouping everything for sysfs 
static struct attribute *attrs[] = {
    &led1_attribute.attr,    // LED1 duty cycle 
    &led2_attribute.attr,    // LED2 duty cycle 
    &led3_attribute.attr,    // LED3 duty cycle 
    &speed_attribute.attr,   // Button press speed 
    NULL,                    
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

/*
 * update_leds function Updates LED states based on current PWM state and duty cycles
 */
static void update_leds(void) {
    if (pwm_state) {
        // LEDs ON state (according to duty cycle) 
        if (led1_duty > 0) gpio_set_value(LED1_PIN, 1);  
        if (led2_duty > 0) gpio_set_value(LED2_PIN, 1);  
        if (led3_duty > 0) gpio_set_value(LED3_PIN, 1);  
    } else {
        // LEDs OFF state
        if (led1_duty < 100) gpio_set_value(LED1_PIN, 0); 
        if (led2_duty < 100) gpio_set_value(LED2_PIN, 0); 
        if (led3_duty < 100) gpio_set_value(LED3_PIN, 0); 
    }
}

// calculate_pwm_timing function calculates PWM ON and OFF durations based on duty cycles
static void calculate_pwm_timing(void) {
  
    u64 period_ns = PWM_PERIOD_NS;  // Total period in nanoseconds
    u64 on_time_ns;                 // ON time duration 
    
    // Get the maximum duty cycle for timing calculation
    int max_duty = led1_duty;
    if (led2_duty > max_duty) max_duty = led2_duty;
    if (led3_duty > max_duty) max_duty = led3_duty;
    
    // Calculate max duty cycle (if all LEDs are at 0%, keep a minimum time)
    on_time_ns = max_duty ? period_ns : 1;
    if (max_duty > 0 && max_duty < 100) {
        u64 temp = period_ns * max_duty;
        do_div(temp, 100);
        on_time_ns = temp;
    }
    
    // Set the timer intervals
    pwm_on_time = ktime_set(0, on_time_ns);             
    pwm_off_time = ktime_set(0, period_ns - on_time_ns); 
}


 //pwm_timer_callback - Timer callback function for PWM control
 //toggles between PWM ON and OFF states and updates LEDs

static enum hrtimer_restart pwm_timer_callback(struct hrtimer *timer) {
    ktime_t now = ktime_get();    // Current time 
    ktime_t interval;             // Next interval duration 
    
    if (pwm_state) {
        
        pwm_state = 0;
        interval = pwm_off_time;
    } else {
        
        pwm_state = 1;
        interval = pwm_on_time;
    }
    
    update_leds();  // Update LED states based on new PWM state 
    
    
    hrtimer_forward(timer, now, interval);
    return HRTIMER_RESTART;  // Keep the timer running 
}

 // button1_handler - Interrupt handler for Button 1
 // Processes Button 1 presses and calculates timing if alternating with Button 2

static irqreturn_t button1_handler(int irq, void *dev_id) {
    current_press_time = ktime_get();  /* Record the current time */
    
    
    if (last_button == 2) {  
        u64 interval_ns = ktime_to_ns(ktime_sub(current_press_time, last_press_time));
        total_press_time += interval_ns;
        valid_alternating_count++;
        
        // Calculate average over last 10 seconds
        if (valid_alternating_count > 0) {
            do_div(total_press_time, valid_alternating_count);
            avg_press_interval = total_press_time;
            total_press_time = avg_press_interval * valid_alternating_count; 
        }
        
        // Reset counters to avoid overflow 
        if (valid_alternating_count > 100) {
            total_press_time = avg_press_interval * 20; // weighted average
            valid_alternating_count = 20;
        }
    }
    
    last_button = 1;  
    last_press_time = current_press_time;
    button_press_count++;
    
    return IRQ_HANDLED;
}

 //button2_handler - Interrupt handler for Button 2
 //Processes Button 2 presses and calculates timing if alternating with Button 1
 
static irqreturn_t button2_handler(int irq, void *dev_id) {
    current_press_time = ktime_get(); 
    
    
    if (last_button == 1) {  
        u64 interval_ns = ktime_to_ns(ktime_sub(current_press_time, last_press_time));
        total_press_time += interval_ns;
        valid_alternating_count++;
        
        // Calculate average over last 10 seconds
        if (valid_alternating_count > 0) {
            do_div(total_press_time, valid_alternating_count);
            avg_press_interval = total_press_time;
            total_press_time = avg_press_interval * valid_alternating_count; 
        }
        
        // Reset counters if to avoid overflow 
        if (valid_alternating_count > 100) {
            total_press_time = avg_press_interval * 20; // average 
            valid_alternating_count = 20;
        }
    }
    
    last_button = 2;  
    last_press_time = current_press_time;
    button_press_count++;
    
    return IRQ_HANDLED;
}

// led1_duty_show - Sysfs show function for LED1 duty cycle
 
static ssize_t led1_duty_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", led1_duty);  // Returns duty cycle
}

 //led1_duty_store - Sysfs store function for LED1 duty cycle
 // Validates and sets the duty cycle for LED1
 
static ssize_t led1_duty_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;
    int duty;
    
    // Converts string to int
    ret = kstrtoint(buf, 10, &duty);
    if (ret < 0)
        return ret;
    
    // Validates duty cycle range
    if (duty < MIN_DUTY || duty > MAX_DUTY)
        return -EINVAL;
    
    led1_duty = duty;  
    calculate_pwm_timing();   
    
    return count;
}

 //led2_duty_show - Sysfs show function for LED2 duty cycle
static ssize_t led2_duty_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", led2_duty); 
}

 //led2_duty_store - Sysfs store function for LED2 duty cycle
 //Validates and sets the duty cycle for LED2
 
static ssize_t led2_duty_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;
    int duty;
    
    
    ret = kstrtoint(buf, 10, &duty);
    if (ret < 0)
        return ret;
    
    
    if (duty < MIN_DUTY || duty > MAX_DUTY)
        return -EINVAL;
    
    led2_duty = duty;  
    calculate_pwm_timing();  
    
    return count;
}

 //led3_duty_show - Sysfs show function for LED3 duty cycle

static ssize_t led3_duty_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", led3_duty);  /* Return current duty cycle */
}

 //led3_duty_store - Sysfs store function for LED3 duty cycle
 //Validates and sets the duty cycle for LED3
 
static ssize_t led3_duty_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;
    int duty;
    
    
    ret = kstrtoint(buf, 10, &duty);
    if (ret < 0)
        return ret;
    
    
    if (duty < MIN_DUTY || duty > MAX_DUTY)
        return -EINVAL;
    
    led3_duty = duty;  
    calculate_pwm_timing();  
    
    return count;
}

//button_speed_show - Sysfs show function for button press speed

static ssize_t button_speed_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    //Calculates button press speed in presses per second 
    u64 speed = 0;
    if (avg_press_interval > 0) {
        // Converts nanoseconds to presses per second
        speed = 1000000000ULL;
        do_div(speed, avg_press_interval);
    }
    
    return sprintf(buf, "%llu\n", speed);
}

 //device_open - Called when the device is opened
 // Prepares the device for reading
 
static int device_open(struct inode *inode, struct file *file) {
    
        if (avg_press_interval > 0) {
        u64 speed = 1000000000ULL;
        do_div(speed, avg_press_interval);
        sprintf(message, "Button Press Speed: %llu presses/second\n", speed);
    } else {
        sprintf(message, "Button Press Speed: 0 presses/second\n");
    }
    
    msg_ptr = message;  
    
    return SUCCESS;
}

 // device_release - Called when the device is closed
 // Performs cleanup when device is closed
 
static int device_release(struct inode *inode, struct file *file) {
    return SUCCESS;
}

 //device_read - Called when the device is read from
 // Sends data from kernel to user space
 
static ssize_t device_read(struct file *filp, char __user *buffer, size_t length, loff_t *offset) {
    int bytes_read = 0;
    
    // End of message
    if (*msg_ptr == 0)
        return 0;
    
    // Copy data to user space
    while (length && *msg_ptr) {
        put_user(*(msg_ptr++), buffer++);  
        length--;
        bytes_read++;
    }
    
    return bytes_read;
}

 //device_write - Called when the device is written to
 // Returns: Number of bytes written

static ssize_t device_write(struct file *filp, const char __user *buffer, size_t length, loff_t *offset) {
    char input[20];
    int led1, led2, led3;
    
    
    if (length > 19)
        return -EINVAL;
    
    
    if (copy_from_user(input, buffer, length))
        return -EFAULT;
    
    input[length] = '\0';  
    
    
    if (sscanf(input, "%d %d %d", &led1, &led2, &led3) == 3) {
        
        if (led1 >= MIN_DUTY && led1 <= MAX_DUTY &&
            led2 >= MIN_DUTY && led2 <= MAX_DUTY &&
            led3 >= MIN_DUTY && led3 <= MAX_DUTY) {
            
            
            led1_duty = led1;
            led2_duty = led2;
            led3_duty = led3;
            calculate_pwm_timing();  
            
            return length;
        }
    }
    
    return -EINVAL; 
}

  // project_init - Initializes the module
 // Sets up device driver, sysfs entries, GPIO, interrupts, and PWM timer

static int __init project_init(void) {
    int ret = 0;
    int button1_irq, button2_irq;
    
    
    major = register_chrdev(0, DEVICE_NAME, &project_fops);
    if (major < 0) {
        pr_alert("Failed to register a major number\n");
        return major;
    }
    pr_info("Registered with major number %d\n", major);
    
    // Creates device class 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    projectClass = class_create(DEVICE_NAME);
#else
    projectClass = class_create(THIS_MODULE, DEVICE_NAME);
#endif
    if (IS_ERR(projectClass)) {
        unregister_chrdev(major, DEVICE_NAME);
        pr_alert("Failed to create class\n");
        return PTR_ERR(projectClass);
    }
    
    // Creates device
    projectDevice = device_create(projectClass, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(projectDevice)) {
        class_destroy(projectClass);
        unregister_chrdev(major, DEVICE_NAME);
        pr_alert("Failed to create device\n");
        return PTR_ERR(projectDevice);
    }
    
    // Creates sysfs entries 
    project_kobj = kobject_create_and_add("pwm_led_controller", kernel_kobj);
    if (!project_kobj) {
        device_destroy(projectClass, MKDEV(major, 0));
        class_destroy(projectClass);
        unregister_chrdev(major, DEVICE_NAME);
        pr_alert("Failed to create kobject\n");
        return -ENOMEM;
    }
    
    ret = sysfs_create_group(project_kobj, &attr_group);
    if (ret) {
        kobject_put(project_kobj);
        device_destroy(projectClass, MKDEV(major, 0));
        class_destroy(projectClass);
        unregister_chrdev(major, DEVICE_NAME);
        pr_alert("Failed to create sysfs group\n");
        return ret;
    }
    
    // Sets up GPIO 
    ret = gpio_request(LED1_PIN, "LED1");
    if (ret) {
        pr_alert("Failed to request LED1 GPIO\n");
        goto fail_gpio;
    }
    ret = gpio_request(LED2_PIN, "LED2");
    if (ret) {
        pr_alert("Failed to request LED2 GPIO\n");
        gpio_free(LED1_PIN);
        goto fail_gpio;
    }
    ret = gpio_request(LED3_PIN, "LED3");
    if (ret) {
        pr_alert("Failed to request LED3 GPIO\n");
        gpio_free(LED2_PIN);
        gpio_free(LED1_PIN);
        goto fail_gpio;
    }
    ret = gpio_request(BTN1_PIN, "BUTTON1");
    if (ret) {
        pr_alert("Failed to request BUTTON1 GPIO\n");
        gpio_free(LED3_PIN);
        gpio_free(LED2_PIN);
        gpio_free(LED1_PIN);
        goto fail_gpio;
    }
    ret = gpio_request(BTN2_PIN, "BUTTON2");
    if (ret) {
        pr_alert("Failed to request BUTTON2 GPIO\n");
        gpio_free(BTN1_PIN);
        gpio_free(LED3_PIN);
        gpio_free(LED2_PIN);
        gpio_free(LED1_PIN);
        goto fail_gpio;
    }
    
    // Configures GPIO directions 
    gpio_direction_output(LED1_PIN, 0);  
    gpio_direction_output(LED2_PIN, 0);
    gpio_direction_output(LED3_PIN, 0);
    gpio_direction_input(BTN1_PIN);      
    gpio_direction_input(BTN2_PIN);
    
    // Sets up button interrupts 
    button1_irq = gpio_to_irq(BTN1_PIN);
    button2_irq = gpio_to_irq(BTN2_PIN);
    
    ret = request_irq(button1_irq, button1_handler, IRQF_TRIGGER_RISING, "button1_handler", NULL);
    if (ret) {
        pr_alert("Failed to request Button1 IRQ\n");
        goto fail_irq;
    }
    
    ret = request_irq(button2_irq, button2_handler, IRQF_TRIGGER_RISING, "button2_handler", NULL);
    if (ret) {
        pr_alert("Failed to request Button2 IRQ\n");
        free_irq(button1_irq, NULL);
        goto fail_irq;
    }
    
    
    last_press_time = ktime_get();
    
    // Initializes PWM timer 
    calculate_pwm_timing();
    hrtimer_init(&pwm_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pwm_timer.function = &pwm_timer_callback;
    hrtimer_start(&pwm_timer, pwm_on_time, HRTIMER_MODE_REL);
    
    pr_info("Project module initialized\n");
    return 0;
    
fail_irq:
    gpio_free(BTN2_PIN);
    gpio_free(BTN1_PIN);
    gpio_free(LED3_PIN);
    gpio_free(LED2_PIN);
    gpio_free(LED1_PIN);
    
fail_gpio:
    sysfs_remove_group(project_kobj, &attr_group);
    kobject_put(project_kobj);
    device_destroy(projectClass, MKDEV(major, 0));
    class_destroy(projectClass);
    unregister_chrdev(major, DEVICE_NAME);
    return ret;
}

// project_exit - Cleanup function called when module is removed
// Cancels timers, releases interrupts and GPIOs, and unregisters devices
 
static void __exit project_exit(void) {
    // Cancels timers
    hrtimer_cancel(&pwm_timer);
    
    // Frees interrupts 
    free_irq(gpio_to_irq(BTN1_PIN), NULL);
    free_irq(gpio_to_irq(BTN2_PIN), NULL);
    
    // Releases GPIO
    gpio_set_value(LED1_PIN, 0);  // Turns off LEDs 
    gpio_set_value(LED2_PIN, 0);
    gpio_set_value(LED3_PIN, 0);
    gpio_free(BTN1_PIN);
    gpio_free(BTN2_PIN);
    gpio_free(LED1_PIN);
    gpio_free(LED2_PIN);
    gpio_free(LED3_PIN);
    
    // Removes sysfs entries 
    sysfs_remove_group(project_kobj, &attr_group);
    kobject_put(project_kobj);
    
    // Destroys device and class 
    device_destroy(projectClass, MKDEV(major, 0));
    class_destroy(projectClass);
    
    // Unregisters character device 
    unregister_chrdev(major, DEVICE_NAME);
    
    pr_info("Project module removed\n");
}

// Registers init and exit functions
module_init(project_init);
module_exit(project_exit);

// Must Add 
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
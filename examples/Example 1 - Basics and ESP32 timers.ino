#include <Arduino.h>
#include <MFIButton.h>

// Define your button pins here
#define BUTTON_1 35
#define BUTTON_2 0

volatile int button1_clicks = 0;
volatile int button1_double_clicks = 0;
volatile int button1_triple_clicks = 0;
volatile int button1_long_presses_1_second = 0;
volatile int button1_long_presses_2_seconds = 0;
volatile int button2_clicks = 0;

// Needed for this ESP32 specific example
hw_timer_t* timer = NULL;

// Step 1: Create one or more button objects
MFIButton button1(BUTTON_1);
MFIButton button2(BUTTON_2);

// Step 2: Create a callback function for various modes you're interested in
void button1_click_callback() {
    // It's a bad idea to do anything time consuming in a callback, as they are
    // called from an interrupt context. Mainly, you should just set a flag or
    // update a simple variable.
    // Also note that these variables are declared as `volatile` to ensure that
    // optimizations by the compiler don't fail due to the fact that code in the
    // loop has no idea that these variables are being updated elsewhere (in an
    // interrupt.)
    button1_clicks++;
}

void button1_double_click_callback() { button1_double_clicks++; }
void button1_triple_click_callback() { button1_triple_clicks++; }

void setup() {
    Serial.begin(115200);
    Serial.println("MFIButton ESP32 example");

    // Step 2: Set various event handlers. These are done per button.
    // First off, examples of functions which are defined elsewhere.
    // For the various sequences, _only_ the callback that matches the user's
    // input will be called. For example, if the user clicks the button twice
    // quickly, only the double click callback will be called.
    button1.onClick(button1_click_callback);
    button1.onDoubleClick(button1_double_click_callback);

    // onClick and onDoubleClick are just easy to use versions of onSequence().
    // onClick() is basically onSequence(1, ) and onDoubleClick() is
    // onSequence(2, ). For anything above 2, you'll need to use onSequence().
    button1.onSequence(3, button1_triple_click_callback);

    // Instead of writing full on functions like above, you can also use lambdas
    button2.onClick([]() {
        // A lambda is basically an anonymous function, which is useful when
        // you don't need to use the function anywhere else. And helps
        // readability when the function is small.
        button2_clicks++;
    });

    // A long press is when the button is held down for a certain amount of
    // time. The first argument is the number of milliseconds to wait before
    // triggering.
    //
    // Since long presses are hard for users to time, the default is for
    // something to happen after the timeout has passed and the button is still
    // pressed, even if the button is being held for longer. That way the user
    // can judge when the desired duration has been reached.
    //
    // So with this configuration a 2+ second press will trigger both the 1
    // second and 2 second callbacks.
    button1.onLongPress(1000, []() { button1_long_presses_1_second++; });
    button1.onLongPress(2000, []() { button1_long_presses_2_seconds++; });

    // For those who want a little more low-level input (possibly for debugging)
    // you can use the following two event handlers. They are called when the
    // button is pressed and released, respectively. So for a double click, you
    // will get press, release, press, and release events. Generally, these are
    // not needed.
    //
    // Serial comms are not recommended in these callbacks, as they are called
    // from an interrupt context, but if you keep them really short, you can
    // get away with it for debugging.
    /*
    button1.onPress([]() { Serial.println("press"); });
    button1.onRelease([]() { Serial.println("release"); });
    */

    // Step 3: Setup the timer code. This example is ESP32 specific, see the
    // documentation for what is needed to use this library on other platforms.
    //
    // Timers are scarce on Arduino, hence leaving this to the user so they can
    // easily integrate this library into code that already uses timers.
    //
    // This setup only has to be done once, no matter the number of buttons
    // you're using with this library.

    // Start a timer 0 (of 4) with a divider of 80, counting up. This will turn
    // the 80MHz timer into a 1 microsecond timer. If your ESP32 has a different
    // clock speed, you'll need to adjust the divider accordingly.
    timer = timerBegin(0, 80, true);
    // When the timer goes off, the MFIButton::timerInterruptHandler() function
    // needs to be called. It will then call the appropriate callbacks for each
    // button. The third argument here is ignored by the ESP-arduino library for
    // now.
    timerAttachInterrupt(timer, MFIButton::timerInterruptHandler, true);
    // And this is how we tell the library to set a timer. The only argument is
    // the duration until the interrupt should fire. The library expects this to
    // be in microseconds.
    MFIButton::setInterruptTimerCallback([](uint16_t duration) {
        // Read the timer's current value. This is the number of microseconds
        // since the timer started counting. This is better than resetting the
        // timer, since that seems to sporadically not work when it is done
        // from inside a timer interrupt.
        uint64_t timer_value = timerRead(timer);
        // Convert the duration to microseconds and add to current value.
        uint64_t next_timer = (1000 * (uint64_t)duration) + timer_value;
        // Set the timer to fire at the calculated value. The third argument
        // tells the timer to fire only once, not repeatedly.
        timerAlarmWrite(timer, next_timer, false);
        // And enable the interrupt firing. We might be able to get away with
        // only doing this once, but enabling it without setting a value seems
        // undefined behavior. And this doesn't hurt anything if it's done
        // multiple times.
        timerAlarmEnable(timer);
    });
    // Here ends the ESP32 specific code.

    // Step 4: Finally start the buttons. This will attempt to bind the pins to
    // a hardware interrupt. If that fails, this will return false.
    if (!button1.begin()) {
        Serial.println("Failed to bind button 1 to hardware interrupt");
        // infinite loop to hang the microcontroller
        while (true) {
        }
    }
    if (!button2.begin()) {
        Serial.println("Failed to bind button 2 to hardware interrupt");
        while (true) {
        }
    }
}

void loop() {
    // Step 4: do something with the data. In this case, we'll just print it.
    if (button1_clicks > 0) {
        Serial.print("Button 1 clicks: ");
        Serial.println(button1_clicks);
        button1_clicks = 0;
    }
    if (button1_double_clicks > 0) {
        Serial.print("Button 1 double clicks: ");
        Serial.println(button1_double_clicks);
        button1_double_clicks = 0;
    }
    if (button1_triple_clicks > 0) {
        Serial.print("Button 1 triple clicks: ");
        Serial.println(button1_triple_clicks);
        button1_triple_clicks = 0;
    }
    if (button1_long_presses_1_second > 0) {
        Serial.print("Button 1 long presses (1 second): ");
        Serial.println(button1_long_presses_1_second);
        button1_long_presses_1_second = 0;
    }
    if (button1_long_presses_2_seconds > 0) {
        Serial.print("Button 1 long presses (2 seconds): ");
        Serial.println(button1_long_presses_2_seconds);
        button1_long_presses_2_seconds = 0;
    }
    if (button2_clicks > 0) {
        Serial.print("Button 2 clicks: ");
        Serial.println(button2_clicks);
        button2_clicks = 0;
    }
    delay(1000);
}

# multifunction-interrupt-button
An Arduino library for easy multifunctional use of buttons, purely using
interrupts.

This library has come into existence because I cannot find any that combine
the following two features:
  * No function calls from `loop()`. Even libraries that support interrupts
    still need some sort of `button.tick()` or `button.update()` in `loop()`
    for full functionality. This does not play well with `delay()` calls or
    light sleeps.
  * Ability to register multiple event handlers on a single button, and
    having only one fire. Some libraries will happily fire both double click
    (twice) and triple click for a single triple click.

Of course, debouncing is a must for any button library.

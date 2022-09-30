#include "MFIButton.h"

#include "assert.h"

struct MFIButton::all_buttons_head_ MFIButton::started_buttons_ =
    SLIST_HEAD_INITIALIZER(started_buttons_);

struct MFIButton::timers_head_ MFIButton::timers_ =
    TAILQ_HEAD_INITIALIZER(timers_);

MFIButton::timer_callback_t MFIButton::set_timer_ = NULL;

bool MFIButton::begin() {
    pinMode(pin_, pullup_ ? INPUT_PULLUP : INPUT);
    this->last_state_ = this->digital_read_();
    // This library doesn't work unless you set up a timer callback
    assert(MFIButton::set_timer_ != NULL);
    // Check if the pin supports interrupts
    if (digitalPinToInterrupt(this->pin_) == NOT_AN_INTERRUPT) {
        return false;
    }
    // Add this button to the list of started buttons, so that
    // the interrupt handler can check it.
    SLIST_INSERT_HEAD(&started_buttons_, this, started_entries_);
    // Attach our pin interrupt handler to the pin
    attachInterrupt(digitalPinToInterrupt(this->pin_),
                    MFIButton::pin_interrupt_handler_, CHANGE);
    return true;
}

void MFIButton::pin_interrupt_handler_() {
    // This won't change throughout the handler, so just read
    // it once.
    auto now = millis();
    // Iterate through all the buttons that have been started
    MFIButton *button = NULL;
    SLIST_FOREACH(button, &started_buttons_, started_entries_) {
        // First check if we are in a debounce period
        if (now - button->last_press_time_ < button->debounce_time_ ||
            now - button->last_release_time_ < button->debounce_time_) {
            // Within debounce period, so we just ignore the whole event.
            // On a press-release-press where only the release is within
            // the debounce period, we will still get a second press event,
            // which will be ignored because it won't be a state change.
            continue;
        }
        // Check if the pin has changed state
        bool state = button->digital_read_();
        if (state != button->last_state_) {
            // We always send onPress and onRelease events
            button->send_press_release_(state);
            if (state != true) {
                // Button pressed is pressed when the pin is LOW
                // If there are long press callbacks, we need to set a timer
                // to check if the button is still pressed after the shortest
                // long press time.
                long_press_t_ *long_press =
                    SLIST_FIRST(&button->long_press_handlers_);
                if (long_press != NULL) {
                    button->set_long_press_timer_(long_press,
                                                  long_press->duration, now);
                }
                button->sequence_clicks_++;
                button->last_press_time_ = now;
            } else {
                // Button released
                // First we need to deterimine if this was a click or a
                // long press. If we're in a sequence already, then we
                // don't allow a switch to long press and do less math.
                // sequence_clicks_ will be 1 after the start of a long
                // press, so only a higher value means we're in a sequence.
                bool is_click = false;
                if (button->sequence_clicks_ > 1) {
                    is_click = true;
                }
                // Break it out like this to avoid loads & math if possible
                if (!is_click) {
                    if (!SLIST_EMPTY(&button->long_press_handlers_)) {
                        uint16_t press_time = now - button->last_press_time_;
                        uint16_t shortest_long_press =
                            SLIST_FIRST(&button->long_press_handlers_)
                                ->duration;
                        if (press_time < shortest_long_press) {
                            is_click = true;
                        }
                    } else {
                        is_click = true;
                    }
                }
                if (is_click) {
                    // This was a click
                    if (button->sequence_clicks_ == button->longest_sequence_) {
                        // If the number of clicks in the sequence is the same
                        // as the longest sequence, we send the sequence event
                        // immediately.
                        button->send_sequence_(button->sequence_clicks_);
                    } else {
                        // If there's still longer sequences, we'll wait for
                        // the delay, and then check if any more presses have
                        // happened.
                        button->add_click_release_timer_(now);
                    }
                } else {
                    // This was a long press, the timer handler should have
                    // handled it already.
                    // TODO: Support only firing a single long press event, even
                    // if multiple long press handlers are registered.
                }
                button->last_release_time_ = now;
            }
            button->last_state_ = state;
        }
    }
}

void MFIButton::timerInterruptHandler() {
    // This won't change throughout the handler, so just read
    // it once.
    auto now = millis();
    // Iterate through all the timers that have been started
    timer_t_ *timer, *tmp;
    TAILQ_FOREACH_SAFE(timer, &MFIButton::timers_, entries, tmp) {
        // Check if the timer has expired
        if (now >= timer->trigger_time) {
            // Timer has expired, so we need to remove it from the list
            TAILQ_REMOVE(&MFIButton::timers_, timer, entries);
            // Switch on type
            switch (timer->type) {
                case timer_type_t_::TIMER_TYPE_SEQUENCE:
                    // This was a click release timer, so we need to check if
                    // there have been any more clicks.
                    timer->button->check_click_release_(timer);
                    break;
                case timer_type_t_::TIMER_TYPE_LONG_PRESS:
                    // This was a long press timer, so we need to check if
                    // the button is still pressed.
                    timer->button->check_long_press_(timer, now);
                    break;
            }
            // Free the timer
            delete timer;
        } else {
            // trigger time is in the future
            break;
        }
    }
    if (timer != NULL) {
        // If there are still timers left, we need to set the next timer
        MFIButton::set_timer_(timer->trigger_time - now);
    }
}

void MFIButton::check_click_release_(timer_t_ *timer) {
    // Check if there have been any more presses since the release for this
    // event
    if (this->last_press_time_ > timer->data.release_time) {
        // There have been more presses, which will have releases, so we
        // will have new timers already. Do nothing.
    } else {
        // There have been no more presses, so we can send the sequence event
        this->send_sequence_(this->sequence_clicks_);
    }
}

void MFIButton::check_long_press_(timer_t_ *timer, unsigned long now) {
    // Check if the button has been released
    if (this->last_release_time_ < this->last_press_time_) {
        // Button is still pressed, so we need to send the long press event
        this->send_long_press_(timer->data.long_press);
        // Since we're now in a long press, we need to reset the
        // sequence clicks.
        this->sequence_clicks_ = 0;
        // Check if there are any more long press handlers
        long_press_t_ *next_long_press =
            SLIST_NEXT(timer->data.long_press, entries);
        if (next_long_press != NULL) {
            uint16_t delay =
                next_long_press->duration - timer->data.long_press->duration;
            this->set_long_press_timer_(next_long_press, delay, now);
        }
    }
    // if it has been released, nothing to do (for now, we might support
    // different long press events)
}

void MFIButton::send_sequence_(uint8_t clicks) {
    // Iterate through all the sequence handlers
    sequence_t_ *handler;
    SLIST_FOREACH(handler, &this->sequence_handlers_, entries) {
        if (handler->clicks == clicks) {
            handler->callback(MFIButtonEvent(MFIButtonEvent::SEQUENCE, this,
                                             (uint16_t)clicks));
            break;
        }
    }
    // reset so we can start a new sequence
    this->sequence_clicks_ = 0;
}

void MFIButton::send_long_press_(long_press_t_ *long_press) {
    long_press->callback(
        MFIButtonEvent(MFIButtonEvent::LONG_PRESS, this, long_press->duration));
}

void MFIButton::add_click_release_timer_(unsigned long now) {

    timer_t_ *timer = new timer_t_;
    timer->trigger_time = now + this->sequence_delay_;
    timer->type = timer_type_t_::TIMER_TYPE_SEQUENCE;
    timer->button = this;
    timer->data.release_time = now;
    MFIButton::insert_timer_(timer, now);
}

void MFIButton::set_long_press_timer_(long_press_t_ *long_press, uint16_t delay,
                                      unsigned long now) {
    // Handlers should be sorted in ascending time order
    timer_t_ *timer = new timer_t_;
    timer->trigger_time = now + delay;
    timer->type = TIMER_TYPE_LONG_PRESS;
    timer->button = this;
    timer->data.long_press = long_press;
    MFIButton::insert_timer_(timer, now);
}

bool MFIButton::digital_read_() {
    // Normally return true if the pin is HIGH, and false if the pin is LOW
    // when inverted, return true if the pin is LOW, and false if the pin is
    // HIGH
    int value = digitalRead(this->pin_);
    bool ret = this->inverted_ ? value == LOW : value == HIGH;
    return ret;
}

void MFIButton::send_press_release_(bool state) {
    if (state != true) {
        // Button pressed
        if (this->on_press_ != NULL) {
            this->on_press_(MFIButtonEvent(MFIButtonEvent::PRESS, this));
        }
    } else {
        // Button released
        if (this->on_release_ != NULL) {
            this->on_release_(MFIButtonEvent(MFIButtonEvent::RELEASE, this));
        }
    }
}

void MFIButton::insert_timer_(timer_t_ *timer, unsigned long now) {
    if (TAILQ_EMPTY(&MFIButton::timers_)) {
        // No timers, so just add it to the list
        TAILQ_INSERT_HEAD(&MFIButton::timers_, timer, entries);
        MFIButton::set_timer_(timer->trigger_time - now);
    } else {
        timer_t_ *t;
        // Iterate through the timers to find the right place to insert
        TAILQ_FOREACH(t, &MFIButton::timers_, entries) {
            if (timer->trigger_time < t->trigger_time) {
                // This timer should be inserted before the current timer
                TAILQ_INSERT_BEFORE(t, timer, entries);
                if (timer == TAILQ_FIRST(&MFIButton::timers_)) {
                    MFIButton::set_timer_(timer->trigger_time - now);
                }
                break;
            }
        }
        if (t == NULL) {
            // This timer should be inserted at the end of the list
            // and since there actually are shorter timers, no need
            // to update the timer interrupt
            TAILQ_INSERT_TAIL(&MFIButton::timers_, timer, entries);
        }
    }
}

void MFIButton::onSequence(uint8_t clicks, event_callback_t callback) {
    struct sequence_t_ *sequence = new sequence_t_;
    sequence->clicks = clicks;
    sequence->callback = callback;
    // Insert/replace the sequence in ascending clicks order.
    // SLIST doesn't have _INSERT_BEFORE, so we have to track
    // the previous element.
    struct sequence_t_ *s, *prev = NULL;
    if (SLIST_EMPTY(&this->sequence_handlers_)) {
        SLIST_INSERT_HEAD(&this->sequence_handlers_, sequence, entries);
    } else {
        SLIST_FOREACH(s, &this->sequence_handlers_, entries) {
            if (clicks < s->clicks) {
                if (prev == NULL) {
                    SLIST_INSERT_HEAD(&this->sequence_handlers_, sequence,
                                      entries);
                } else {
                    SLIST_INSERT_AFTER(prev, sequence, entries);
                }
                break;
            }
            if (clicks == s->clicks) {
                s->callback = callback;
                // We don't need the new sequence, so free it
                delete sequence;
                break;
            }
            prev = s;
        }
        // s will be NULL if we reached the end of the list, and if we reached
        // the end of the list, that means we haven't inserted or updated.
        if (s == NULL) {
            SLIST_INSERT_AFTER(prev, sequence, entries);
        }
    }
    // Do this after the insert, to avoid the race condition where
    // the code assumes this amount of clicks is in the config, when
    // it hasn't been inserted yet. This way the just inserted
    // handler will be ignored until after this next bit is done.
    if (this->longest_sequence_ < clicks) {
        this->longest_sequence_ = clicks;
    }
}

void MFIButton::onSequence(uint8_t clicks, callback_t callback) {
    this->onSequence(clicks, (event_callback_t)callback);
}

void MFIButton::onClick(event_callback_t callback) {
    this->onSequence(1, callback);
}

void MFIButton::onClick(callback_t callback) {
    this->onSequence(1, (event_callback_t)callback);
}

void MFIButton::onDoubleClick(event_callback_t callback) {
    this->onSequence(2, callback);
}

void MFIButton::onDoubleClick(callback_t callback) {
    this->onSequence(2, (event_callback_t)callback);
}

void MFIButton::onLongPress(uint16_t duration, event_callback_t callback) {
    struct long_press_t_ *long_press = new long_press_t_;
    long_press->duration = duration;
    long_press->callback = callback;
    // Insert the long press in ascending duration order.
    // SLIST doesn't have _INSERT_BEFORE, so we have to track
    // the previous element.
    struct long_press_t_ *l, *prev = NULL;
    if (SLIST_EMPTY(&this->long_press_handlers_)) {
        SLIST_INSERT_HEAD(&this->long_press_handlers_, long_press, entries);
    } else {
        SLIST_FOREACH(l, &this->long_press_handlers_, entries) {
            if (duration < l->duration) {
                if (prev == NULL) {
                    SLIST_INSERT_HEAD(&this->long_press_handlers_, long_press,
                                      entries);
                } else {
                    SLIST_INSERT_AFTER(prev, long_press, entries);
                }
                break;
            }
            if (duration == l->duration) {
                l->callback = callback;
                // We don't need the new long press, so free it
                delete long_press;
                break;
            }
            prev = l;
        }
        // l will be NULL if we reached the end of the list, and if we reached
        // the end of the list, that means we haven't inserted.
        if (l == NULL) {
            SLIST_INSERT_AFTER(prev, long_press, entries);
        }
    }
    // Just like with sequences, we need to make sure the handler
    // is inserted before we up the duration.
    if (longest_long_press_ < duration) {
        longest_long_press_ = duration;
    }
}

void MFIButton::onLongPress(uint16_t duration, callback_t callback) {
    this->onLongPress(duration, (event_callback_t)callback);
}

void MFIButton::onPress(event_callback_t callback) {
    this->on_press_ = callback;
}

void MFIButton::onPress(callback_t callback) {
    this->on_press_ = (event_callback_t)callback;
}

void MFIButton::onRelease(event_callback_t callback) {
    this->on_release_ = callback;
}

void MFIButton::onRelease(callback_t callback) {
    this->on_release_ = (event_callback_t)callback;
}

void MFIButton::setInterruptTimerCallback(timer_callback_t callback) {
    MFIButton::set_timer_ = callback;
}

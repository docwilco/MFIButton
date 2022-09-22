#ifndef _MFIBUTTON_H
#define _MFIBUTTON_H

#include "Arduino.h"
#include "bsd/queue.h"

#define MFI_BUTTON_DEFAULT_DEBOUNCE 35
#define MFI_BUTTON_DEFAULT_SEQUENCE_DELAY 250

class MFIButton;

class MFIButtonEvent {
   public:
    enum Type {
        PRESS,
        RELEASE,
        CLICK,
        LONG_PRESS,
        SEQUENCE,
    };
    MFIButtonEvent(Type type, MFIButton *button, uint16_t value = 0) : type_(type), value_(value), button_(button) {}
    Type type() const { return this->type_; }
    uint16_t value() const { return this->value_; }

   protected:
    Type type_;
    uint16_t value_;
    MFIButton *button_;
};

class MFIButton {
   public:
    typedef void (*callback_t)();
    typedef void (*event_callback_t)(MFIButtonEvent &);
    typedef void (*timer_callback_t)(uint16_t, callback_t);

    // Constructor
    MFIButton(int pin, bool pullup = true, bool inverted = false);

    // Methods
    static void set_timer_callback(timer_callback_t callback);

    bool begin();
    void onPress(event_callback_t callback);
    void onRelease(event_callback_t callback);
    void onSequence(uint8_t clicks, event_callback_t callback);
    void onClick(event_callback_t callback);
    void onDoubleClick(event_callback_t callback);
    void onLongPress(uint16_t duration, event_callback_t callback);
   private:
    enum timer_type_t_ {
        TIMER_TYPE_LONG_PRESS,
        TIMER_TYPE_SEQUENCE,
    };
    // These two use SLIST to save memory. It would be nice to have
    // _INSERT_BEFORE, but we'll just have to use _INSERT_AFTER and
    // pay the price of keeping a pointer to previous while we're 
    // inserting. It's only done during setup, so that's fine.
    struct sequence_t_ {
        uint8_t clicks;
        event_callback_t callback;
        SLIST_ENTRY(sequence_t_) entries;
    };
    struct long_press_t_ {
        uint16_t duration;
        event_callback_t callback;
        SLIST_ENTRY(long_press_t_) entries;
    };
    struct timer_t_ {
        unsigned long trigger_time;
        timer_type_t_ type;
        MFIButton *button;
        union {
            long_press_t_ *long_press;
            unsigned long release_time;
        } data;
        LIST_ENTRY(timer_t_) entries;
    };

    bool inverted_;
    bool last_state_;
    uint8_t pin_;
    uint8_t longest_sequence_ = 0;
    uint8_t sequence_clicks_ = 0;
    uint16_t longest_long_press_ = 0;
    uint16_t debounce_time_ = MFI_BUTTON_DEFAULT_DEBOUNCE;
    uint16_t sequence_delay_ = MFI_BUTTON_DEFAULT_SEQUENCE_DELAY;
    unsigned long last_press_time_ = 0;
    unsigned long last_release_time_ = 0;
    SLIST_HEAD(sequences_head_, sequence_t_) sequence_handlers_ = SLIST_HEAD_INITIALIZER(sequence_handlers_);
    SLIST_HEAD(long_presss_head_, long_press_t_) long_press_handlers_ = SLIST_HEAD_INITIALIZER(long_press_handlers_);
    SLIST_ENTRY(MFIButton) started_entries_;
    event_callback_t on_press_ = NULL;
    event_callback_t on_release_ = NULL;

    bool digital_read_();
    void send_press_release_(bool state);
    void send_sequence_(uint8_t clicks);
    void send_long_press_(long_press_t_ *long_press);
    void set_long_press_timer_(long_press_t_ *long_press, uint16_t delay, unsigned long now);
    void add_click_release_timer_(unsigned long now);
    void check_click_release_(timer_t_ *timer);
    void check_long_press_(timer_t_ *timer, unsigned long now);

    static timer_callback_t set_timer_;
    static void pin_interrupt_handler_();
    static void timer_interrupt_handler_();

    static void insert_timer_(timer_t_ *timer);
};

#endif  // _MFIBUTTON_H

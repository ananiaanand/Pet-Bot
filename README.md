# Pet-Bot
ESP32 Pet Bot is an interactive Arduino project featuring voice commands, touch interaction, and productivity tools. It combines OLED facial expressions with servo movement and a built-in Pomodoro timer.
Voice-Controlled Movement: Servo motor (GPIO25) drives forward (0°), backward (180°), or stops (90°) with smooth transitions

Interactive OLED Faces: Neutral default cycles to happy/surprised on touch; sad face during Pomodoro work phase

Smart Switch: Single-click shows local weather (temp/humidity); double-click launches 25min work → beep → 5min break cycle

Pomodoro Timer: Progress bars, buzzer alerts (GPIO33), touch-to-snooze; returns to neutral face after break

WiFi Weather: Real-time Kochi, IN forecasts with error handling for offline use

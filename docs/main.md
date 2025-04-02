## Notes

1. Heartbeat
2. RTOS
3. Battery management
4. Trigger system
   1. Wakeword trigger
      1. **AI:** Parallel sampling and DAC usage to be confirm.
      2. Activity persistance
      3. [Reference](https://www.youtube.com/watch?v=re-dSV_a0tM)
   2. Button activity persistance
   3. Long press trigger
   4. button press delay
5. **Order multiple boards**
6. Explore RTOS
   1. **AI:**List all tasks
   2. Reuse espresif RTOS
7. 1926 board selection decision

## Decisions

1. Wakeword in MVP
   1. Want to talk to it from far
   2. Much better experiece
   3. **Verdict:** Selected
      1. Leave conteneous conversation capability (interruption while playback) for MVP
      2. Current capability:
         1. Trigger using wakeword
            1. Wakenet
               1. **AI** Try the demo
         2. listen till person is speaking
            1. Research
               1. Amplitude integration
               2. Other methods
         3. process response
            1. Sending audio
               1. WAV vs MP3
            2. input audio processing
               1. Openai API
         4. playback response
            1. text generation
               1. LLM
            2. playback
               1. Eleven-labs
            3. DAC with AMP with Speaker
         5. do not listen while playback
            1. Interupt with button.
2. Supporting app
   1. Wifi configuration
      1. Standard method
   2. Manual entry of SSID
   3. Manual entry of user and parent info
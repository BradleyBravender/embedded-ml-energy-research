# Deployment

## Purpose
This project runs a TinyML keyword spotting model on the ESP32 and benchmarks:

- inference latency  
- feature extraction latency  
- full pipeline latency  
- memory usage (arena, heap, stack)  
- external power consumption (via GPIO measurement)  

The model recognizes:
- **"yes"**
- **"no"**
- (and additional classes such as silence/unknown depending on the model)

---

# Benchmark Overview

The application runs three tests:

## Test 1 — YES (Precomputed Features)
- Uses precomputed features for a "yes" sample  
- Bypasses the audio pipeline  
- Measures **model inference only**

---

## Test 2 — NO (Precomputed Features)
- Same as Test 1, but with a "no" sample  
- Used to validate correctness and consistency  

---

## Test 3 — Full Pipeline (Audio → Features → Model)
- Uses embedded raw audio  
- Runs full preprocessing pipeline  
- Measures:
  - feature extraction time  
  - inference time  
  - total pipeline time  

---

# Power Measurement

A GPIO pin is used to enable external power measurement:

- **GPIO 33 HIGH → active measurement window**

### Measurement behavior

| Test | Measurement Scope |
|------|------------------|
| Test 1 & 2 | Inference only (`Invoke()`) |
| Test 3 | Full pipeline (features + inference) |

Power should be measured externally (e.g., shunt resistor + oscilloscope or Analog Discovery).

---

# Deploy to ESP32

## Install ESP-IDF

Follow the official guide:  
https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html

Ensure:
- `idf.py` is available. Once esp-idf is installed, this can be done with:  
  ```
  source ~/esp/esp-idf/export.sh
  ```
- environment variables are set  

---

## Build, Flash, and Monitor (while in `micro_speech/` project directory)

On Alex's computer (MacOS):
```bash
idf.py build && idf.py -p /dev/cu.usbserial-0001 flash monitor
```

To build on Bradley's computer (Ubuntu 24.04):
```
rm -rf build managed_components dependencies.lock
idf.py reconfigure
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Full Testing Workflow
1. Ensure everything is wired correctly.
2. Load the model of interest into `deployment/micro_speech/main/model.cc`.
3. Run the WaveForms collection script, and then the test script.
4. Record the WaveForms output, and the serial monitor output for the test.
5. Once all tests are completed, parse the outputs into the results spreadsheet. 
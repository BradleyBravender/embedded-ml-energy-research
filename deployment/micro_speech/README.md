# ESP32 TinyML Inference Benchmark

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
- `idf.py` is available  
- environment variables are set  

---

## Build the project

```bash
idf.py set-target esp32
idf.py build

# Training

## Purpose
This folder uses a pipeline to train custom-sized ESP32-optimized CNNs for voice detection.

## How to use
1. First, start a Docker container.
   ```
   docker run --gpus all -it \
    -p 8888:8888 -p 6006:6006 \
    -v /media/brad/Backup1/voice:/voice \
    tensorflow/tensorflow:2.19.0-gpu-jupyter \
    jupyter notebook --ip=0.0.0.0 --allow-root \
    --notebook-dir=/voice --NotebookApp.token=''
    ```
2. Open the training notebook. 
   - Can access in the container by pasting `http://127.0.0.1:8888/tree` in your browser.
   - Notebook location: `/tflite-micro/tensorflow/lite/micro/examples/micro_speech/train/train_micro_speech_model.ipynb`
3. After working through the training pipeline, copy the quantized model and paste it in the model deployment program.

## Aditional notes
This pipeline was modified to allow the training of variable-sized models.

The source of modification is found in the following programs:
- `./tflite-micro/tensorflow/lite/micro/examples/micro_speech/train/tensorflow/tensorflow/examples/speech_commands/models.py`
- `./tflite-micro/tensorflow/lite/micro/examples/micro_speech/train/tensorflow/tensorflow/examples/speech_commands/train.py`

The `trained_models` folder is the output folder that houses the models generated in the training pipeline contained in `tflite-micro`.

There may be permission issues when running `git add .` If this is the case, file permissions can be removed via:
- `sudo chmod 777 <folder_name>`,
- Or, more globally, `sudo chown -R $USER:$USER .`

## Model parametrization


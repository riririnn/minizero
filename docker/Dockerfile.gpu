# This docker file is to build a zero training environment
FROM nvidia/cuda:12.8.1-cudnn-devel-ubuntu22.04

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Taipei

ENV CUDA_HOME=/usr/local/cuda
ENV CUDA_NVCC_EXECUTABLE=/usr/local/cuda/bin/nvcc
ENV PATH=/usr/local/cuda/bin:${PATH}
ENV LD_LIBRARY_PATH=/usr/local/cuda/lib64:${LD_LIBRARY_PATH}

# 1. 必要なシステムパッケージを一度にインストール（build-essential, unzip, jre等を追加）
RUN rm /etc/apt/sources.list.d/cuda.list || true && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
    vim gdb git less htop wget cmake ffmpeg clang-format tmux \
    python3-pip python3-dev python3-opencv libopencv-dev \
    libarchive-tools libboost-all-dev build-essential \
    unzip gconf2 default-jre && \
    ln -s /usr/bin/python3 /usr/bin/python && \
    python3 -m pip install -U pip && \
    python3 -m pip cache purge && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# 2. PyTorchのインストール (C++用 LibTorch もここに内包されます)
RUN python3 -m pip install --no-cache-dir \
    torch==2.11.0 \
    torchvision==0.26.0 \
    torchaudio==2.11.0 \
    --index-url https://download.pytorch.org/whl/cu128

# 3. pybind11 とその他の Python パッケージ
RUN python3 -m pip install --no-cache-dir pybind11==3.0.4 timm einops gdown numpy pandas jupyter matplotlib

# 4. CMake に pip 経由で入れた Torch と pybind11 の場所を教える (ここが重要)
ENV Torch_DIR=/usr/local/lib/python3.10/dist-packages/torch/share/cmake/Torch
ENV pybind11_DIR=/usr/local/lib/python3.10/dist-packages/pybind11/share/cmake/pybind11
ENV CMAKE_PREFIX_PATH=/usr/local/lib/python3.10/dist-packages/torch/share/cmake:/usr/local/lib/python3.10/dist-packages/pybind11/share/cmake

RUN nvcc --version && \
    python3 - <<'PY'
import torch
print("torch:", torch.__version__)
print("torch cuda:", torch.version.cuda)
assert torch.version.cuda == "12.8", torch.version.cuda
PY

# 5. install gym/atari & Arcade Learning Environment
RUN python3 -m pip install gym[atari] && \
    python3 -m pip cache purge && \
    cd /opt/ && \
    git clone --branch v0.8.1 --depth 1 https://github.com/mgbellemare/Arcade-Learning-Environment.git && \
    cd Arcade-Learning-Environment && \
    mkdir build && cd build && \
    cmake ../ -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DBUILD_PYTHON_LIB=OFF && \
    cmake --build . --target install -j && \
    cd /opt/ && rm -rf Arcade-Learning-Environment

# 6. install gogui
RUN cd /opt/ && \
    gdown https://drive.google.com/uc?id=1wF65yyD5BjL0MkZRFCWLvmqWYx8TDgk0 && \
    unzip gogui-1.4.9.zip && \
    echo "exit 0" >> gogui-1.4.9/install.sh && \
    cd gogui-1.4.9 && \
    ./install.sh && \
    cd .. && \
    rm -rf gogui-1.4.9 gogui-1.4.9.zip

RUN python3 -m pip install gradio matplotlib

RUN apt-get update && \
    apt-get install -y --no-install-recommends fonts-ipafont-mincho && \
    rm -rf /var/lib/apt/lists/*

RUN mkdir /workspace
WORKDIR /workspace
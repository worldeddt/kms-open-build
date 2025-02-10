FROM ubuntu:22.04

#RUN apt update && apt install -y build-essential cmake

ENV DEBIAN_FRONTEND=noninteractive

# 빌드에 필요한 패키지 설치
#RUN pt install -y --no-install-recommends git\
#    apt update && \
#    apt install -y --no-install-recommends build-essential && \
#    apt install -y --no-install-recommends cmake && \
#    apt install -y --no-install-recommends maven && \
#    apt install -y --no-install-recommends git \
#    && rm -rf /var/lib/apt/lists/*

# 빌드

WORKDIR /opt/kurento

COPY . .

EXPOSE 8888

#RUN git clone https://github.com/Kurento/kms-cmake-utils.git && \
#    ch kms-cmake-utils && \
#    mkdir build && \
#    cd build && \
#    cmake .. && \
#    make && \
#    make install

# ...
# 빌드된 kurento-media-server 실행 파일이 /usr/bin/kurento-media-server 등에 설치되었을 것
# 이를 ENTRYPOINT 등으로 설정

#ENTRYPOINT ["/usr/bin/kurento-media-server"]
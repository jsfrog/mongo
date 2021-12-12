# mongodb 源码分析
基于mongodb`4.4`分支, 加了些中文的注释和流程图。

# building
我这里是linux系统。
- 系统: debian 10.6
- vscode: 1.61.1
- python: 3.7.3
- mongodb: 4.4
- gcc: 8.3.0
- pip: 20.x
## 安装依赖
```shell
sudo apt install  libcurl4-openssl-dev
sudo apt install liblzma-dev
cd mongo
# 安装mongod mongo mongos 修改了源码也需要重新编译
python3 buildscripts/scons.py install-core
```
## 修改配置
我这里加了.vscode和config目录，只要修改mongodb.conf的dbPath和systemLog.path为你的本地路径就可以了。




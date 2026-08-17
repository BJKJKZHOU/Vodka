# JustCanFd

## Qt 路径

本项目使用 Qt 5.14.2，qmake 路径为：

```text
/home/zhouheng/Qt5.14.2/5.14.2/gcc_64/bin/qmake
```

## 编译

```bash
cd /home/zhouheng/GitHub_Pro/Vodka/dataengines/justcanfd
mkdir -p build
cd build
make distclean 2>/dev/null || true
/home/zhouheng/Qt5.14.2/5.14.2/gcc_64/bin/qmake \
    ../justcanfd.pro CONFIG+=release
make -j"$(nproc)"
```

编译生成的动态库位于 `build` 目录。

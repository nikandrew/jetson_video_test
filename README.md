# Просмотр CSI-камеры на Jetson Nano

Минимальное C++-приложение показывает изображение с CSI-камеры через аппаратный
GStreamer-конвейер NVIDIA. Значения по умолчанию: CAM0 (`sensor-id=0`),
1920×1080, 30 кадров/с.

## Важно для Raspberry Pi HQ Camera

Raspberry Pi HQ Camera 1.0 использует сенсор **Sony IMX477**. Одного физического
подключения к J13 недостаточно: в установленном образе Jetson Linux должны быть
драйвер IMX477 и соответствующий Device Tree для Jetson Nano B01. Если камера
не появляется в Argus, эта программа также не сможет получить изображение.

Устанавливать драйвер следует именно под используемую версию JetPack/L4T и
плату P3448-0000 в carrier board P3450 B01. После изменения Device Tree Jetson
нужно перезагрузить.

## Сборка на Jetson Nano

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libgstreamer1.0-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

Компоненты `nvarguscamerasrc`, `nvvidconv`, `nvegltransform` и
`nveglglessink` устанавливаются вместе с JetPack. Сборка предназначена для
выполнения непосредственно на Jetson Nano (ARM64), а не на Windows-компьютере.

## Запуск

Запускайте из графической сессии Jetson:

```bash
./build/jetson_camera_viewer
```

Остановка — `Ctrl+C` в терминале, из которого запущена программа. Доступные
параметры:

```bash
./build/jetson_camera_viewer --help
./build/jetson_camera_viewer --sensor-id 0 --width 1920 --height 1080 --fps 30
./build/jetson_camera_viewer --flip-method 2
```

`flip-method`: 0 — без преобразования, 1/3 — поворот на 90/270°, 2 — 180°,
4/5/6/7 — варианты отражения.

Разрешение и FPS должны совпадать с одним из режимов, предоставляемых
установленным драйвером IMX477. Если 1920×1080@30 недоступен, выберите режим из
вывода `nvarguscamerasrc` при запуске диагностической команды ниже.

## Проверка камеры без программы

Сначала убедитесь, что нужные NVIDIA-плагины доступны:

```bash
gst-inspect-1.0 nvarguscamerasrc
```

Затем проверьте тот же конвейер напрямую:

```bash
gst-launch-1.0 -e nvarguscamerasrc sensor-id=0 \
  ! 'video/x-raw(memory:NVMM),width=1920,height=1080,format=NV12,framerate=30/1' \
  ! nvvidconv ! nvegltransform ! nveglglessink sync=false
```

Полезные проверки:

```bash
ls -l /dev/video*
sudo dmesg | grep -i -E 'imx477|camera|tegra-capture'
systemctl status nvargus-daemon
```

Если появляется `No cameras available`, чаще всего причина в отсутствующем или
неподходящем драйвере/Device Tree, неверно вставленном шлейфе либо выборе не того
`sensor-id`. Если Argus завис после неудачного запуска, его можно перезапустить:

```bash
sudo systemctl restart nvargus-daemon
```

Для показа EGL-окна необходима запущенная графическая сессия и корректная
переменная `DISPLAY` (обычно `:0`). По чистому SSH без X-сессии окно не откроется.

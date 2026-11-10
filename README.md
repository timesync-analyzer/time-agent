# timesync-agent

Linux-агент для сбора метрик синхронизации времени и состояния узла.
Читает сообщения `ptp4l`, `phc2sys` и `ppswatch`, собирает системные
показатели и передаёт их серверу `timesync-analyzer` через ZeroMQ в формате
Protocol Buffers. В режиме `journal` агент наблюдает за уже запущенными
службами; параметры синхронизации задаются в самих службах linuxptp.

Более подробное описание принципа работы можно найти [тут](https://git.redlab-i.ru/zloyaloha/timesync-analyzer/src/master/docs/ВКР-текст.pdf)

## Собираемые данные

| Источник | Данные |
| --- | --- |
| `ptp4l` | Смещение часов, частотная поправка, задержка пути, состояние сервопривода |
| События `ptp4l` | Переходы состояния порта, имя интерфейса и модель сетевого адаптера |
| `phc2sys` | Смещение, частотная поправка, задержка, состояние сервопривода |
| `ppswatch` | Смещение PPS в наносекундах |
| `/proc/stat` | Загрузка CPU, накопленные счётчики переключений контекста, прерываний и softirq |
| `/proc/meminfo` | Доступная и свободная память, swap, буферы; значения в KiB |
| `/sys/class/net` | Накопленные счётчики пакетов, потерь, ошибок и коллизий выбранного интерфейса |
| `/sys/class/hwmon` | Температуры выбранных датчиков в тысячных долях °C |
| `pmc` | Снимок PTP-топологии: local/parent/grandmaster clock identity, порты, stepsRemoved и meanPathDelay |

Системные показатели опрашиваются с интервалом `settings.poll_timeout_ms`.
Сетевой интерфейс выбирается из события смены состояния порта `ptp4l`,
содержащего имя интерфейса. До такого события сетевые метрики отсутствуют.

При включённом `ptp_topology.on` снимок топологии запрашивается после сообщения
`ptp4l` о выборе лучшего master, включая выбор локальных часов. Для этого
выполняются запросы `GET DEFAULT_DATA_SET`, `GET PARENT_DATA_SET`,
`GET CURRENT_DATA_SET` и `GET PORT_DATA_SET` через `pmc`.
Периодического опроса топологии нет. Обнаружение foreign master записывается
в лог, отдельное сообщение об этом серверу не отправляется.

## Сборка

Все команды ниже выполняются из корня репозитория `time-agent`.
Схема Protobuf находится в git-подмодуле `protocol`:

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DTIME_AGENT_BUILD_TESTS=OFF -DTIME_AGENT_BUILD_DOCS=OFF
cmake --build build --parallel
```

Исполняемый файл: `build/time_agent`. Генерация `metrics.pb.h` и `metrics.pb.cc`
выполняется CMake автоматически; вручную запускать `protoc` не требуется.

Опции CMake:

| Опция | По умолчанию | Назначение |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | `Release` | `Release`: оптимизация `-O2`; `Debug`: отладочная информация, AddressSanitizer и UBSan |
| `TIME_AGENT_BUILD_TESTS` | `ON` | Сборка тестов с GoogleTest v1.14.0; при конфигурации требуется доступ к GitHub для загрузки GoogleTest |
| `TIME_AGENT_BUILD_DOCS` | `ON` | Генерация HTML через Doxygen, если он установлен |

Установка бинарного файла:

```sh
sudo cmake --install build --prefix /usr/local
```

Конфигурация и unit systemd автоматически не устанавливаются.

## Конфигурация и запуск

Скопируйте шаблон и укажите имя узла, адрес анализатора и доступные источники:

```sh
cp configs/config.yaml configs/local_config.yaml
```

Пример конфигурации с чтением журнала `ptp4l` и `phc2sys`:

```yaml
global:
  node: "node-01"
settings:
  poll_timeout_ms: 10000
  log_level: "info"
temperature:
  sensors:
    - name: "coretemp"
zmq:
  endpoint: "tcp://192.168.1.10:10000"
  queue_size: 10000
  timeout_after_close_ms: 1000
ptp_topology:
  on: true
  pmc_path: "/usr/sbin/pmc"
  uds_path: "/var/run/ptp4lro"
  domain_number: 0
  boundary_hops: 0
services:
  ptp4l:
    on: true
    source: "journal"
  phc2sys:
    on: true
    source: "journal"
  ppswatch:
    on: false
    source: "subproccess"
    dev: "/dev/pps0"
    executable_path: "/usr/bin/ppswatch"
```

| Параметр | Значение |
| --- | --- |
| `global.node` | Имя узла в каждом отправленном сообщении; задавайте уникальное имя для каждого агента |
| `settings.poll_timeout_ms` | Положительный интервал сбора системных метрик в миллисекундах |
| `settings.log_level` | `debug`, `info`, `warn` или `error`; неизвестное значение трактуется как `info` |
| `temperature.sensors[].name` | Имена из файлов `/sys/class/hwmon/hwmon*/name`; читаются зоны с файлами `temp*_label` и `temp*_input` |
| `zmq.endpoint` | Адрес PULL-сокета анализатора, к которому подключается агент |
| `zmq.queue_size` | High-water mark очереди отправки ZeroMQ в сообщениях |
| `zmq.timeout_after_close_ms` | Время ожидания отправки очереди при закрытии сокета (linger), мс |
| `ptp_topology.on` | Включение запросов топологии; без секции по умолчанию `false` |
| `ptp_topology.pmc_path` | Путь к `pmc`; по умолчанию `/usr/sbin/pmc` |
| `ptp_topology.uds_path` | Сокет управления работающего `ptp4l`; по умолчанию `/var/run/ptp4lro` |
| `ptp_topology.config_path` | Необязательный путь к конфигурации linuxptp, передаваемый `pmc -f` |
| `ptp_topology.domain_number` | PTP-домен для `pmc -d`; по умолчанию `0` |
| `ptp_topology.boundary_hops` | Значение `pmc -b`; по умолчанию `0` |
| `services.<name>.on` | Включение коллектора `ptp4l`, `phc2sys` или `ppswatch` |
| `services.<name>.source` | `journal` для `ptp4l`/`phc2sys`; `subproccess` для запуска дочернего процесса |
| `services.ppswatch.dev` | PPS-устройство, передаваемое `ppswatch` |
| `services.ppswatch.executable_path` | Путь к `ppswatch`; если не задан, используется `/usr/bin/ppswatch` |

Перед запуском проверьте, что службы пишут сообщения в journal, адрес
анализатора доступен, а `uds_path` и `domain_number` соответствуют настройкам
`ptp4l`. Пути и имена в `configs/*_config.yaml` относятся к конкретным узлам
стенда и требуют адаптации.

```sh
sudo ./build/time_agent configs/local_config.yaml
```

Единственный позиционный аргумент — путь к YAML. Без аргумента используется
`../config.yaml` относительно рабочего каталога. Всегда передавайте путь явно:
при ошибке чтения или разбора YAML агент переходит к минимальной конфигурации
по умолчанию, которая не задаёт все необходимые параметры рабочего запуска.

Для чтения journal без root пользователь должен иметь доступ к журналу,
например через группу `systemd-journal`. Доступ к PPS-устройству и сокету `pmc`
также определяется правами ОС. В режиме дочерних процессов агент без root
запускает команды через `/usr/bin/sudo`.

Логи выводятся в консоль. Остановка — `Ctrl+C`, `SIGINT` или `SIGTERM`.
Оставьте хотя бы один рабочий коллектор службы включённым: после завершения
всех потоков чтения завершается и цикл системных метрик.

## Запуск через systemd

После установки бинарного файла сохраните рабочую конфигурацию в
`/etc/time-agent/config.yaml`. Создайте `/etc/systemd/system/time_agent.service`:

```ini
[Unit]
Description=Time synchronization metrics agent
After=network.target

[Service]
ExecStart=/usr/local/bin/time_agent /etc/time-agent/config.yaml
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

В этом примере служба работает от root. В репозитории также есть
`configs/time_agent.service`, но его `ExecStart` содержит абсолютные пути
конкретного стенда.

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now time_agent.service
sudo systemctl status time_agent.service
sudo journalctl -u time_agent.service -f
```

После изменения YAML перезапустите службу: `sudo systemctl restart time_agent.service`.

## Протокол передачи

Агент подключается ZeroMQ PUSH-сокетом к серверному PULL-сокету. Каждое
сообщение содержит сериализованный `MetricsWrapper`: тип, временную метку
`google.protobuf.Timestamp`, имя узла и один payload (`ptp4l`, `phc2sys`,
`system`, `pps`, `ptp4l_port_event` или `ptp_topology`).

Схема: [protocol/proto/metrics.proto](protocol/proto/metrics.proto).
При изменении схемы необходимо согласованно обновлять подмодуль протокола
у агента и анализатора. Локального хранилища метрик нет; очередь ZeroMQ
не заменяет долговременное хранение. При недоступном получателе и заполнении
очереди блокирующая отправка может задерживать сбор данных.

## Структура репозитория

- `src/main.cpp` — запуск, создание коллекторов, обработка сигналов.
- `src/agent.cpp` — потоки чтения, обработчики событий и отправка метрик.
- `src/collector.cpp` — journal, дочерние процессы и парсеры сообщений.
- `src/system_metrics.cpp` — сбор CPU, памяти, сети и температур.
- `src/ptp_topology.cpp` — запросы `pmc` и разбор топологии.
- `src/adapter.cpp` — преобразование в Protobuf и транспорт ZeroMQ.
- `src/config.cpp` — чтение YAML.
- `src/network_adapter_resolver.cpp` — определение модели сетевого адаптера.
- `include/` — заголовки и описание API.
- `configs/` — примеры конфигурации агента, linuxptp и systemd.
- `protocol/` — подмодуль со схемой сообщений.
- `tests/` — unit- и интеграционные тесты.
- `scripts/` — вспомогательные сценарии нагрузки CPU и сетевого интерфейса.
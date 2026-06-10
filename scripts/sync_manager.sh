#!/usr/bin/env bash
#
# sync_manager.sh — интерактивное управление PTP-синхронизацией на стенде.
#
# Запускается с управляющего сервера. В интерактивном диалоге:
#   1) выбирается узел-мастер из списка стенда;
#   2) выбирается сетевой интерфейс, на котором работает ptp4l;
#   3) на каждый узел по ssh заливается /etc/linuxptp/ptp4l.conf
#      (мастер получает пониженный priority1, остальные — обычный);
#   4) ptp4l перезапускается, чтобы конфиг применился.
#
# Узлы адресуются по hostname, как в run_iperf_sequence.sh.
#
# Переменные окружения (с дефолтами):
#   SSH_USER          пользователь ssh (default: zloyaloha)
#   NODES             список узлов через пробел
#                     (default: "zip-bookworm alpha mouse-d12 hawk-d12")
#   IFACE             интерфейс ДЛЯ ВСЕХ узлов; если задан — диалог
#                     о выборе интерфейсов пропускается (один на всех)
#   IFACE_DEFAULT     дефолт интерфейса в диалоге (default: rteth0)
#   MASTER            узел-мастер; если задан — диалог о выборе пропускается
#   SYNC_MODE         pps|ptp — способ синхронизации слейвов; если задан,
#                     диалог о нём пропускается
#   PPS_DEV           PPS-устройство на слейвах (default: /dev/stand/pps)
#   PPSCTL            бинарь ppsctl на узлах (default: ppsctl)
#   FREE_RUNNING      1/0 — free-running phc2sys на слейвах; если задан,
#                     диалог о нём пропускается (по умолчанию зависит от SYNC_MODE)
#   MASTER_PRIORITY1  priority1 для мастера (default: 64)
#   SLAVE_PRIORITY1   priority1 для слейвов (default: 128)
#   TX_TS_TIMEOUT     tx_timestamp_timeout в конфиге ptp4l, мс (default: 50)
#                     помогает от "timed out while polling for tx timestamp"
#   PTP4L_CONF        путь к конфигу ptp4l на узле (default: /etc/linuxptp/ptp4l.conf)
#   PTP4L_SERVICE     имя systemd-сервиса ptp4l (default: ptp4l)
#   PHC2SYS_CONF      путь к конфигу phc2sys (default: /etc/linuxptp/phc2sys.conf)
#   PHC2SYS_SERVICE   имя systemd-сервиса phc2sys (default: phc2sys)
#   RESTART           1 — перезапускать ptp4l/phc2sys, 0 — только залить (default: 1)
#   DRY_RUN           1 — ничего не менять на узлах, только показать план (default: 0)
#   ASSUME_YES        1 — не спрашивать финальное подтверждение (default: 0)
#
set -euo pipefail

SSH_USER="${SSH_USER:-zloyaloha}"
NODES="${NODES:-zip-bookworm alpha mouse-d12 hawk-d12}"
IFACE="${IFACE:-}"
IFACE_DEFAULT="${IFACE_DEFAULT:-rteth0}"
MASTER="${MASTER:-}"
SYNC_MODE="${SYNC_MODE:-}"           # pps | ptp
PPS_DEV="${PPS_DEV:-/dev/stand/pps}"
PPSCTL="${PPSCTL:-ppsctl}"
FREE_RUNNING="${FREE_RUNNING:-}"
MASTER_PRIORITY1="${MASTER_PRIORITY1:-64}"
SLAVE_PRIORITY1="${SLAVE_PRIORITY1:-128}"
TX_TS_TIMEOUT="${TX_TS_TIMEOUT:-50}"  # tx_timestamp_timeout (мс) в ptp4l.conf
PTP4L_CONF="${PTP4L_CONF:-/etc/linuxptp/ptp4l.conf}"
PTP4L_SERVICE="${PTP4L_SERVICE:-ptp4l}"
PHC2SYS_CONF="${PHC2SYS_CONF:-/etc/linuxptp/phc2sys.conf}"
PHC2SYS_SERVICE="${PHC2SYS_SERVICE:-phc2sys}"
SUDO_PASS_PROVIDED="${SUDO_PASS+yes}"   # был ли пароль передан через env
SUDO_PASS="${SUDO_PASS:-}"              # sudo-пароль для узлов; если не задан — спросим
# phc2sys в автоматическом режиме (мастер всегда; слейвы при free_running=нет)
PHC2SYS_AUTO_OPTS="${PHC2SYS_AUTO_OPTS:--a -r -r -l 6 -N 10 -R 1 -E pi -P 0.7 -I 0.3 -u 1}"
RESTART="${RESTART:-1}"
DRY_RUN="${DRY_RUN:-0}"
ASSUME_YES="${ASSUME_YES:-0}"

read -r -a NODE_ARR <<<"${NODES}"
# Интерфейс каждого узла; индексы параллельны NODE_ARR (заполняется в choose_ifaces).
NODE_IFACE_ARR=()

# ---------------------------------------------------------------------------
# Вспомогательные функции
# ---------------------------------------------------------------------------
log()  { echo "[$(date '+%F %T')] $*"; }
err()  { echo "[$(date '+%F %T')] !! $*" >&2; }
die()  { err "$*"; exit 1; }

usage() {
    cat <<EOF
Usage: $0 [-u ssh_user] [-s pps|ptp] [-i iface] [-m master]
          [-n "node1 node2 ..."] [-p master_priority1] [-f|-F] [-y] [-d]

  -u  ssh-пользователь                      (env SSH_USER, default: ${SSH_USER})
  -s  способ синхронизации: pps|ptp          (env SYNC_MODE)
  -i  интерфейс для ВСЕХ узлов (пропуск диалога) (env IFACE)
  -m  узел-мастер (пропустить диалог)        (env MASTER)
  -n  список узлов через пробел             (env NODES)
  -p  priority1 для мастера                 (env MASTER_PRIORITY1, default: ${MASTER_PRIORITY1})
  -f  free-running phc2sys на слейвах        (env FREE_RUNNING=1)
  -F  без free-running (-a -r на всех)       (env FREE_RUNNING=0)
  -y  не спрашивать подтверждение           (env ASSUME_YES=1)
  -d  dry-run, только показать план         (env DRY_RUN=1)
  -h  показать эту справку

Примеры:
  $0                                  # полностью интерактивно
  $0 -s pps -m alpha -i rteth0 -y     # без диалога, синхронизация по PPS
  DRY_RUN=1 $0 -s ptp -m hawk-d12 -i eth0
EOF
}

while getopts ":u:s:i:m:n:p:fFydh" opt; do
    case "${opt}" in
        u) SSH_USER="${OPTARG}" ;;
        s) SYNC_MODE="${OPTARG}" ;;
        i) IFACE="${OPTARG}" ;;
        m) MASTER="${OPTARG}" ;;
        n) NODES="${OPTARG}"; read -r -a NODE_ARR <<<"${NODES}" ;;
        p) MASTER_PRIORITY1="${OPTARG}" ;;
        f) FREE_RUNNING=1 ;;
        F) FREE_RUNNING=0 ;;
        y) ASSUME_YES=1 ;;
        d) DRY_RUN=1 ;;
        h) usage; exit 0 ;;
        :) die "опция -${OPTARG} требует аргумент" ;;
        *) usage; exit 1 ;;
    esac
done

ssh_run() {
    local host="$1"; shift
    ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
        "${SSH_USER}@${host}" "$@"
}

# Выполнить команду на узле под sudo. Пароль (SUDO_PASS, может быть пустым при
# passwordless sudo) подаётся в sudo -S через stdin, поэтому сама команда не
# должна читать stdin. Команда передаётся как один аргумент.
ssh_sudo() {
    local host="$1" cmd="$2"
    printf '%s\n' "${SUDO_PASS}" \
        | ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
              "${SSH_USER}@${host}" "sudo -S -p '' sh -c \"${cmd}\""
}

# проверка, что значение есть в массиве узлов
node_known() {
    local needle="$1" n
    for n in "${NODE_ARR[@]}"; do
        [[ "${n}" == "${needle}" ]] && return 0
    done
    return 1
}

# ---------------------------------------------------------------------------
# Шаблон конфига. priority1 подставляется первым аргументом ($1).
# Секция порта [iface] добавляется отдельно — общая для мастера и слейвов.
# ---------------------------------------------------------------------------
render_config() {
    local prio1="$1" iface="$2"
    cat <<EOF
[global]
#
# Default Data Set
#
twoStepFlag             1
slaveOnly               0
socket_priority         0
priority1               ${prio1}
priority2               128
domainNumber            0
#utc_offset             37
clockClass              248
clockAccuracy           0xFE
offsetScaledLogVariance 0xFFFF
free_running            0
freq_est_interval       1
dscp_event              0
dscp_general            0
dataset_comparison      ieee1588
G.8275.defaultDS.localPriority  128
maxStepsRemoved         255
#
# Port Data Set
#
logAnnounceInterval     1
logSyncInterval         0
operLogSyncInterval     0
logMinDelayReqInterval  0
logMinPdelayReqInterval 0
operLogPdelayReqInterval 0
announceReceiptTimeout  3
syncReceiptTimeout      0
delayAsymmetry          0
fault_reset_interval    4
neighborPropDelayThresh 20000000
masterOnly              0
G.8275.portDS.localPriority     128
asCapable               auto
BMCA                    ptp
inhibit_announce        0
inhibit_delay_req       0
ignore_source_id        0
#
# Run time options
#
assume_two_step         0
logging_level           6
path_trace_enabled      0
follow_up_info          0
hybrid_e2e              0
inhibit_multicast_service       0
net_sync_monitor        0
tc_spanning_tree        0
tx_timestamp_timeout    ${TX_TS_TIMEOUT}
unicast_listen          0
unicast_master_table    0
unicast_req_duration    3600
use_syslog              1
verbose                 0
summary_interval        0
kernel_leap             1
check_fup_sync          0
#
# Servo Options
#
pi_proportional_const   0.0
pi_integral_const       0.0
pi_proportional_scale   0.0
pi_proportional_exponent        -0.3
pi_proportional_norm_max        0.7
pi_integral_scale       0.0
pi_integral_exponent    0.4
pi_integral_norm_max    0.3
step_threshold          0.0
first_step_threshold    0.00002
max_frequency           900000000
clock_servo             pi
sanity_freq_limit       200000000
ntpshm_segment          0
msg_interval_request    0
servo_num_offset_values 10
servo_offset_threshold  0
write_phase_mode        0
#
# Transport options
#
transportSpecific       0x0
ptp_dst_mac             01:1B:19:00:00:00
p2p_dst_mac             01:80:C2:00:00:0E
udp_ttl                 1
udp6_scope              0x0E
uds_address             /var/run/ptp4l
#
# Default interface options
#
clock_type              OC
network_transport       UDPv4
delay_mechanism         E2E
time_stamping           hardware
tsproc_mode             filter
delay_filter            moving_median
delay_filter_length     10
egressLatency           0
ingressLatency          0
boundary_clock_jbod     0
#
# Clock description
#
productDescription      ;;
revisionData            ;;
manufacturerIdentity    00:00:00
userDescription         ;
timeSource              0xA0
#
# Port
#
[${iface}]
EOF
}

# ---------------------------------------------------------------------------
# Шаблон phc2sys.conf (EnvironmentFile с PHC2SYS_OPTS для systemd-юнита).
# Слейв во free-running режиме синхронизирует CLOCK_REALTIME от PHC интерфейса;
# во всех остальных случаях — автоматический режим (-a -r ...).
# ---------------------------------------------------------------------------
render_phc2sys() {
    local role="$1" iface="$2" opts
    if [[ "${role}" == "slave" && "${FREE_RUNNING}" == "1" ]]; then
        opts="-s ${iface} -c CLOCK_REALTIME --free_running 1 --uds_address /run/ptp4l -w"
    else
        opts="${PHC2SYS_AUTO_OPTS}"
    fi
    printf 'PHC2SYS_OPTS="%s"\n' "${opts}"
}

# человекочитаемое описание режима phc2sys для плана
phc2sys_mode() {
    local role="$1" iface="$2"
    if [[ "${role}" == "slave" && "${FREE_RUNNING}" == "1" ]]; then
        echo "free-running (-s ${iface})"
    else
        echo "auto (-a -r)"
    fi
}

# команда ppsctl для слейва в зависимости от способа синхронизации.
# pps: -bfc (slave дисциплинируется от PPS), ptp: -BF.
ppsctl_cmd() {
    case "${SYNC_MODE}" in
        pps) echo "${PPSCTL} -bfc ${PPS_DEV}" ;;
        ptp) echo "${PPSCTL} -BF ${PPS_DEV}" ;;
        *)   echo "" ;;
    esac
}

# интерфейс узла по имени (по параллельным массивам)
iface_for() {
    local needle="$1" i
    for i in "${!NODE_ARR[@]}"; do
        if [[ "${NODE_ARR[$i]}" == "${needle}" ]]; then
            echo "${NODE_IFACE_ARR[$i]}"
            return 0
        fi
    done
    return 1
}

# ---------------------------------------------------------------------------
# Предварительный выбор способа синхронизации: pps | ptp
# ---------------------------------------------------------------------------
choose_sync_mode() {
    if [[ -n "${SYNC_MODE}" ]]; then
        case "${SYNC_MODE}" in
            pps|ptp) ;;
            *) die "SYNC_MODE должен быть pps или ptp (получено: ${SYNC_MODE})" ;;
        esac
        return
    fi
    echo "Способ синхронизации:" >&2
    echo "  1) pps — слейвы дисциплинируются от PPS (ppsctl -bfc), PTP во free_running" >&2
    echo "  2) ptp — синхронизация по PTP (ppsctl -BF)" >&2
    local choice
    while true; do
        read -r -p "Выберите [1-2]: " choice
        case "${choice}" in
            1) SYNC_MODE="pps"; break ;;
            2) SYNC_MODE="ptp"; break ;;
            *) echo "  некорректный ввод" >&2 ;;
        esac
    done
}

# ---------------------------------------------------------------------------
# Интерактивный выбор мастера
# ---------------------------------------------------------------------------
choose_master() {
    if [[ -n "${MASTER}" ]]; then
        node_known "${MASTER}" || die "узел '${MASTER}' не входит в список: ${NODES}"
        return
    fi
    echo "Узлы стенда:" >&2
    local i
    for i in "${!NODE_ARR[@]}"; do
        printf "  %d) %s\n" "$((i + 1))" "${NODE_ARR[$i]}" >&2
    done
    local choice
    while true; do
        read -r -p "Выберите узел-мастер [1-${#NODE_ARR[@]}]: " choice
        if [[ "${choice}" =~ ^[0-9]+$ ]] \
           && (( choice >= 1 && choice <= ${#NODE_ARR[@]} )); then
            MASTER="${NODE_ARR[$((choice - 1))]}"
            break
        fi
        echo "  некорректный ввод, попробуйте ещё раз" >&2
    done
}

# ---------------------------------------------------------------------------
# Интерактивный выбор интерфейса для каждого узла
# ---------------------------------------------------------------------------
choose_ifaces() {
    local i node ans
    for i in "${!NODE_ARR[@]}"; do
        node="${NODE_ARR[$i]}"
        if [[ -n "${IFACE}" ]]; then
            # один интерфейс на всех — диалог пропускаем
            NODE_IFACE_ARR[$i]="${IFACE}"
            continue
        fi
        read -r -p "Интерфейс на ${node} [${IFACE_DEFAULT}]: " ans
        NODE_IFACE_ARR[$i]="${ans:-${IFACE_DEFAULT}}"
    done
}

# ---------------------------------------------------------------------------
# Интерактивный выбор режима free-running для слейвов
# ---------------------------------------------------------------------------
choose_free_running() {
    if [[ -n "${FREE_RUNNING}" ]]; then
        # нормализуем произвольный ввод из env к 1/0
        case "${FREE_RUNNING}" in
            1|y|Y|yes|true) FREE_RUNNING=1 ;;
            *)              FREE_RUNNING=0 ;;
        esac
        return
    fi
    # дефолт зависит от способа синхронизации: pps ⇒ да, ptp ⇒ нет
    local ans def_yes=0 prompt="[y/N]"
    if [[ "${SYNC_MODE}" == "pps" ]]; then
        def_yes=1; prompt="[Y/n]"
    fi
    read -r -p "Free running на слейвах? ${prompt}: " ans
    if [[ -z "${ans}" ]]; then
        FREE_RUNNING="${def_yes}"
    elif [[ "${ans}" =~ ^[Yy]$ ]]; then
        FREE_RUNNING=1
    else
        FREE_RUNNING=0
    fi
}

# Проверить sudo-пароль на узле через `sudo -v`. Возвращает:
#   0   — пароль принят (или passwordless);
#   255 — узел недоступен по ssh;
#   иначе — пароль неверный / нет права sudo.
validate_sudo_on() {
    local host="$1" rc=0
    printf '%s\n' "${SUDO_PASS}" \
        | ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
              "${SSH_USER}@${host}" "sudo -S -p '' -v" >/dev/null 2>&1 || rc=$?
    return "${rc}"
}

# ---------------------------------------------------------------------------
# Запрос sudo-пароля (один на все узлы) с проверкой на первом узле.
# Пропускается в dry-run. При неверном пароле — повторный ввод (до 3 раз).
# ---------------------------------------------------------------------------
choose_sudo() {
    [[ "${DRY_RUN}" == "1" ]] && return
    local host="${NODE_ARR[0]}" tries=0 rc

    while true; do
        if [[ "${SUDO_PASS_PROVIDED}" != "yes" ]]; then
            read -r -s -p "sudo-пароль для ${SSH_USER} на узлах (Enter если passwordless): " SUDO_PASS
            echo
        fi

        rc=0
        validate_sudo_on "${host}" || rc=$?
        case "${rc}" in
            0)
                log "sudo проверен на ${host}"
                return 0
                ;;
            255)
                err "узел ${host} недоступен по ssh для проверки sudo — продолжаю без неё"
                return 0
                ;;
            *)
                # неверный пароль — заставляем ввести заново
                SUDO_PASS_PROVIDED=""
                tries=$((tries + 1))
                (( tries >= 3 )) && die "sudo не принят на ${host} после ${tries} попыток"
                err "sudo не принят на ${host}, попробуйте ещё раз"
                ;;
        esac
    done
}

# ---------------------------------------------------------------------------
# Применение конфига на одном узле
# ---------------------------------------------------------------------------
apply_node() {
    local host="$1" role="$2" iface="$3" prio1
    if [[ "${role}" == "master" ]]; then
        prio1="${MASTER_PRIORITY1}"
    else
        prio1="${SLAVE_PRIORITY1}"
    fi

    log "узел ${host}: роль=${role}, priority1=${prio1}, iface=${iface}, phc2sys=$(phc2sys_mode "${role}" "${iface}")"

    if [[ "${DRY_RUN}" == "1" ]]; then
        log "  [dry-run] конфиги НЕ заливаются, сервисы НЕ перезапускаются"
        return 0
    fi

    # Заливаем во временный файл, затем sudo install на место —
    # так не нужен root для самой передачи, и запись атомарна.
    local tmp_ptp="/tmp/ptp4l.conf.$$"
    local tmp_phc="/tmp/phc2sys.conf.$$"

    # --- ptp4l.conf ---
    if ! render_config "${prio1}" "${iface}" \
            | ssh_run "${host}" "cat > '${tmp_ptp}'"; then
        err "  не удалось передать ptp4l.conf на ${host}"
        return 1
    fi
    if ! ssh_sudo "${host}" "install -D -m 0644 '${tmp_ptp}' '${PTP4L_CONF}'"; then
        err "  не удалось установить ${PTP4L_CONF} на ${host} (sudo/права?)"
        return 1
    fi
    ssh_run "${host}" "rm -f '${tmp_ptp}'" || true
    log "  ptp4l.conf записан в ${PTP4L_CONF}"

    # --- phc2sys.conf ---
    if ! render_phc2sys "${role}" "${iface}" \
            | ssh_run "${host}" "cat > '${tmp_phc}'"; then
        err "  не удалось передать phc2sys.conf на ${host}"
        return 1
    fi
    if ! ssh_sudo "${host}" "install -D -m 0644 '${tmp_phc}' '${PHC2SYS_CONF}'"; then
        err "  не удалось установить ${PHC2SYS_CONF} на ${host} (sudo/права?)"
        return 1
    fi
    ssh_run "${host}" "rm -f '${tmp_phc}'" || true
    log "  phc2sys.conf записан в ${PHC2SYS_CONF} ($(phc2sys_mode "${role}" "${iface}"))"

    if [[ "${RESTART}" == "1" ]]; then
        # порядок важен: ptp4l поднимается раньше phc2sys
        if ssh_sudo "${host}" "systemctl restart '${PTP4L_SERVICE}'"; then
            log "  сервис ${PTP4L_SERVICE} перезапущен"
        else
            err "  не удалось перезапустить ${PTP4L_SERVICE} на ${host}"
            return 1
        fi
        if ssh_sudo "${host}" "systemctl restart '${PHC2SYS_SERVICE}'"; then
            log "  сервис ${PHC2SYS_SERVICE} перезапущен"
        else
            err "  не удалось перезапустить ${PHC2SYS_SERVICE} на ${host}"
            return 1
        fi
    fi

    # --- ppsctl: только на слейвах, флаги зависят от способа синхронизации ---
    if [[ "${role}" == "slave" ]]; then
        local pps; pps="$(ppsctl_cmd)"
        if [[ -n "${pps}" ]]; then
            if ssh_sudo "${host}" "${pps}"; then
                log "  выполнено: ${pps}"
            else
                err "  не удалось выполнить '${pps}' на ${host}"
                return 1
            fi
        fi
    fi
    return 0
}

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
(( ${#NODE_ARR[@]} > 0 )) || die "список узлов пуст"

choose_sync_mode
choose_master
choose_ifaces
choose_free_running

echo
echo "=================== План применения ==================="
echo "  ssh-пользователь : ${SSH_USER}"
echo "  синхронизация    : ${SYNC_MODE}  (ppsctl на слейвах: $(ppsctl_cmd))"
echo "  free-running     : $([[ "${FREE_RUNNING}" == "1" ]] && echo "да (на слейвах)" || echo нет)"
echo "  ptp4l.conf       : ${PTP4L_CONF}  (tx_timestamp_timeout=${TX_TS_TIMEOUT})"
echo "  phc2sys.conf     : ${PHC2SYS_CONF}"
echo "  рестарт сервисов : $([[ "${RESTART}" == "1" ]] && echo да || echo нет)"
echo "  dry-run          : $([[ "${DRY_RUN}" == "1" ]] && echo да || echo нет)"
echo "  -------------------------------------------------------"
for i in "${!NODE_ARR[@]}"; do
    node="${NODE_ARR[$i]}"
    iface="${NODE_IFACE_ARR[$i]}"
    if [[ "${node}" == "${MASTER}" ]]; then
        printf "  %-16s МАСТЕР  priority1=%-4s iface=%-8s phc2sys: %s\n" \
            "${node}" "${MASTER_PRIORITY1}" "${iface}" "$(phc2sys_mode master "${iface}")"
    else
        printf "  %-16s slave   priority1=%-4s iface=%-8s phc2sys: %-22s ppsctl: %s\n" \
            "${node}" "${SLAVE_PRIORITY1}" "${iface}" "$(phc2sys_mode slave "${iface}")" "$(ppsctl_cmd)"
    fi
done
echo "======================================================="
echo

if [[ "${ASSUME_YES}" != "1" && "${DRY_RUN}" != "1" ]]; then
    read -r -p "Применить? [y/N]: " confirm
    [[ "${confirm}" =~ ^[Yy]$ ]] || die "отменено пользователем"
fi

choose_sudo

rc=0
for i in "${!NODE_ARR[@]}"; do
    node="${NODE_ARR[$i]}"
    iface="${NODE_IFACE_ARR[$i]}"
    role="slave"
    [[ "${node}" == "${MASTER}" ]] && role="master"
    if ! apply_node "${node}" "${role}" "${iface}"; then
        err "узел ${node}: применение не удалось"
        rc=1
    fi
    echo
done

if (( rc == 0 )); then
    log "готово: конфиги применены на все узлы (мастер: ${MASTER})"
else
    err "завершено с ошибками — см. вывод выше"
fi
exit "${rc}"
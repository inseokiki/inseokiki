#!/usr/bin/env python3
"""Reproducible AWGN coverage campaign; Python standard library only.

This measures current implementation behavior, not standards conformance.
Never reads or modifies config/sim_config.txt. Outputs must be a new directory.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import subprocess
import time
from datetime import datetime, timezone

PHY = Path(__file__).resolve().parents[1]
PAIR = ['ber', 'bler']
HARQ = ['ber_final', 'bler_first', 'bler_harq', 'avg_tx']


def cases():
    result = []

    def add(name, channel, metrics, **cfg):
        result.append(dict(id=name, channel=channel, metrics=metrics,
                           config=dict(PHYSICAL_CHANNEL=channel, **cfg)))

    add('pbch', 'PBCH', ['bler'])
    for space, levels in [('CSS', [4, 8, 16]), ('USS', [1, 2, 4, 8, 16])]:
        for al in levels:
            add(f'pdcch_{space.lower()}_al{al}', 'PDCCH',
                ['p_detect', 'p_miss', 'p_false_alarm', 'ber_detected'],
                SEARCH_SPACE=space, PDCCH_AL=al, DCI_SIZE=39)
    mods = [('qpsk', 'TABLE1', 5), ('16qam', 'TABLE1', 10),
            ('64qam', 'TABLE1', 20), ('256qam', 'TABLE2', 20)]
    for mod, table, mcs in mods:
        sch = dict(MCS_TABLE=table, MCS_INDEX=mcs)
        add(f'pdsch_coded_{mod}', 'PDSCH', PAIR, USE_DMRS=0, TB_SIZE=256, **sch)
        for eq in ['ZF', 'MMSE']:
            add(f'pdsch_dmrs_{mod}_{eq.lower()}', 'PDSCH', PAIR,
                USE_DMRS=1, EQUALIZER=eq, **sch)
            for tp in [0, 1]:
                add(f'pusch_tp{tp}_{mod}_{eq.lower()}', 'PUSCH', PAIR,
                    TRANSFORM_PRECODING=tp, EQUALIZER=eq, **sch)
        add(f'ber_{mod}', 'BER', PAIR, MODULATION=mod.upper(), CODING='NONE')
    for rv in ['IR', 'CHASE']:
        hq = dict(HARQ_ENABLE=1, HARQ_MAX_RETX=4, HARQ_RV_SEQUENCE=rv)
        add(f'pdsch_harq_{rv.lower()}', 'PDSCH', HARQ, USE_DMRS=1, **hq)
        for tp in [0, 1]:
            add(f'pusch_tp{tp}_harq_{rv.lower()}', 'PUSCH', HARQ,
                TRANSFORM_PRECODING=tp, **hq)
    add('pdsch_olla', 'PDSCH', [], OLLA_ENABLE=1)
    add('pdsch_table3', 'PDSCH', PAIR, USE_DMRS=1, MCS_TABLE='TABLE3', MCS_INDEX=5)
    add('pusch_table3', 'PUSCH', PAIR, MCS_TABLE='TABLE3', MCS_INDEX=5)
    for fmt in range(4):
        for bits in ([1, 2] if fmt < 2 else [3, 11]):
            add(f'pucch_f{fmt}_uci{bits}', 'PUCCH', ['uci_ber'],
                PUCCH_FORMAT=fmt, PUCCH_UCI_BITS=bits,
                PUCCH_NUM_SYMBOLS=2 if fmt == 2 else 4, PUCCH_NUM_PRB=1)
    for fmt in ['SHORT', 'LONG']:
        add(f'prach_{fmt.lower()}', 'PRACH', ['preamble_error_rate', 'ta_mae_samples'],
            PRACH_FORMAT=fmt, PRACH_ROOT_SEQ_INDEX=1,
            PRACH_NUM_CS=13, PRACH_MAX_DELAY_SAMPLES=8)
    for row in range(1, 5):
        add(f'csirs_row{row}', 'CSIRS', ['mse_pilots_db', 'mse_all_sc_db'], CSIRS_ROW=row)
    for comb in [2, 4]:
        add(f'srs_comb{comb}', 'SRS', ['mse_pilots_db', 'mse_all_sc_db', 'theory_db'],
            SRS_COMB=comb, SRS_BW_RB=16)
    for coding in ['NONE', 'LDPC', 'POLAR']:
        add(f'legacy_{coding.lower()}', 'NONE', PAIR, CODING=coding, MODULATION='QPSK')
    return result


def parse_output(output, case, snrs):
    """Reject missing, malformed, duplicate, nonfinite or unexpected data rows."""
    if 'simulation complete.' not in output.lower():
        raise ValueError('missing completion marker')
    if case['id'] == 'pdsch_olla':
        found = re.findall(r'(Open-Loop|OLLA) 최종: BER=(\S+)\s+BLER=(\S+).*?평균 MCS=(\S+)', output)
        if len(found) != 2 or [f[0] for f in found] != ['Open-Loop', 'OLLA'] or len(snrs) != 1:
            raise ValueError('missing OLLA pass summaries')
        records = [(snrs[0], f'{mode.lower().replace("-", "_")}_{metric}', value)
                   for mode, *values in found
                   for metric, value in zip(['ber', 'bler', 'avg_mcs'], values)]
    else:
        header = re.search(r'^\s*SNR\s*\(dB\).*$', output, re.M)
        if not header:
            raise ValueError('missing metric header')
        rows = []
        for line in output[header.end():].splitlines():
            words = line.split()
            if not words or not re.match(r'^[+-]?\d', words[0]):
                continue
            if len(words) != len(case['metrics']) + 1:
                raise ValueError(f'unexpected columns: {line}')
            rows.append(words)
        if [float(r[0]) for r in rows] != snrs:
            raise ValueError(f'incomplete/duplicate SNR grid: {rows}')
        records = [(float(row[0]), metric, value)
                   for row in rows for metric, value in zip(case['metrics'], row[1:])]
    for _, metric, raw in records:
        value = float(raw)
        if not math.isfinite(value):
            raise ValueError(f'nonfinite {metric}')
        if ('ber' in metric or 'bler' in metric or metric.startswith('p_') or metric == 'preamble_error_rate') and not 0 <= value <= 1:
            raise ValueError(f'invalid rate {metric}={value}')
        if metric == 'avg_tx' and not 1 <= value <= 4:
            raise ValueError(f'invalid attempt count {value}')
    for snr in snrs:
        values = {metric: float(raw) for s, metric, raw in records if s == snr}
        if 'bler_harq' in values and values['bler_harq'] > values['bler_first'] + 0.0001:
            raise ValueError('HARQ final BLER exceeds first transmission BLER')
        if 'p_detect' in values and abs(sum(values[k] for k in ['p_detect', 'p_miss', 'p_false_alarm']) - 1) > 0.0002:
            raise ValueError('PDCCH event probabilities do not sum to one')
    return records


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_summary(out, manifest, records):
    snrs = manifest['snr_db']
    n = manifest['trials_per_seed_snr']
    seeds = manifest['seeds']
    lines = ['# AWGN 전체 지원 채널 측정', '',
             f'현재 구현의 {len(manifest["cases"])}개 대표 설정, {len(manifest["runs"])}회 실행, '
             f'{len(records)}개 지표 측정값을 수집했다. 모든 실행이 정상 종료하고 예상 SNR 행 수·유한값·확률 범위 검사를 통과했다.', '',
             '## 시험 조건', '',
             f'- SNR: {snrs} dB. 시뮬레이터 입력 SNR이며 Eb/N0로 변환하지 않았다.',
             f'- Seed: {seeds}, seed/SNR별 {n}회. 설정당 SNR별 총 {n * len(seeds)}회.',
             '- 공통: RB 24, SCS 30 kHz, FFT 1024, SISO. 실제 자원 사용량은 채널별 로그 참조.',
             '- PDSCH/PUSCH: TABLE1 MCS 5/10/20, TABLE2 MCS 20으로 QPSK/16QAM/64QAM/256QAM 대표점. TABLE3 MCS 5 추가.',
             '- HARQ: QPSK, MMSE, IR/Chase, 최대 4회 전송. OLLA: SNR별 독립 실행, 각 pass의 최종 결과만 수집.',
             '- 전체 채널/지원 형식의 대표 경로를 포함한다. 모든 MCS·RB·UCI 크기 조합을 전수 검사한 것은 아니다.', '',
             '## 범위와 구현 한계', '',
             '| 대상 | 포함 범위 |', '|---|---|',
             '| PBCH | Polar AWGN |',
             '| PDCCH | CSS AL 4/8/16, USS AL 1/2/4/8/16, blind decoding |',
             '| PDSCH | coded 기준, DMRS SISO ZF/MMSE, IR/Chase HARQ, SISO OLLA |',
             '| PUSCH | SISO CP/DFT-s, ZF/MMSE, IR/Chase HARQ |',
             '| PUCCH | Format 0/1 UCI 1/2 bits, Format 2/3 UCI 3/11 bits |',
             '| PRACH | SHORT/LONG, preamble detection 및 TA |',
             '| CSI-RS / SRS | CSI-RS row 1~4, SRS comb 2/4 |',
             '| 보조 기준 | uncoded BER 4변조, legacy OFDM NONE/LDPC/POLAR |', '',
             'MIMO·beamforming 경로는 AWGN 설정을 받아도 자체 공간/페이딩 채널을 생성하므로 순수 AWGN 자료에서 제외했다. '
             'UL 4포트 코드북은 AWGN 자체를 지원하지 않는다. ULPC는 링크 잡음 측정이 아닌 전력 제어 시계열이다.', '',
             'PUCCH 0/1은 근사 Zadoff–Chu 기반 시퀀스이고, 2/3은 원래 short-block 코딩 대신 Polar를 사용한 연구 모델이다. '
             'CSI-RS row 매핑도 현재 코드의 단순화된 구현 범위를 측정한다. '
             '다수 물리채널은 RE 도메인이며 CP/시간영역 전파 효과 검증으로 해석하지 않는다. '
             '이 자료는 현재 Tx/Rx 구현의 기준 측정이며 3GPP 적합성 인증 결과가 아니다.', '',
             '## 지표 해석', '',
             '- CSV 한 행은 case/seed/SNR/metric 하나다. 원본 출력 문자열은 reported_value에 보존했다.',
             '- BER/BLER/UCI BER/preamble error는 서로 다른 모집단이므로 채널 간 하나의 평균으로 합치지 않았다.',
             '- PDCCH ber_detected는 검출된 DCI에 한정한다. 검출 0건일 때 C 코드가 출력하는 0은 BER 성능 증거가 아니다. '
             'p_detect/p_miss/p_false_alarm을 함께 읽어야 한다. false alarm은 이 구현의 trial 분류율이며 빈 검색공간당 확률이 아니다.',
             '- HARQ ber_final은 최종 복호 BER, bler_first/harq는 첫 전송/최종 TB 실패율, avg_tx는 TB당 전송 횟수다.',
             '- CSI-RS/SRS MSE는 dB, TA MAE는 samples. legacy 시험의 반복 단위는 NUM_OFDM_SYMBOLS다.',
             '- 출력 반올림 정밀도를 넘는 정확한 오류 개수나 신뢰구간을 역산하지 않았다. 관측 오류 0은 실제 오류율 0을 뜻하지 않는다.',
             '- SNR별 같은 RNG 흐름에서 연속 측정하므로 그리드가 바뀌면 후속 SNR 결과도 달라질 수 있다. '
             'OLLA 두 pass는 서로 다른 난수 표본이며 고정 MCS BLER나 수렴 보장으로 해석하지 않는다.', '',
             '## 대표 지표', '',
             '아래는 seed별 출력의 산술평균이다. MSE(dB)는 선형 전력으로 평균한 뒤 dB로 변환했다. '
             '모든 지표와 중간 SNR은 [results.csv](results.csv)에 있다.', '',
             f'| Case | 지표 | {snrs[0]} dB | {snrs[-1]} dB |', '|---|---|---:|---:|']
    for case in manifest['cases']:
        metrics = case['metrics']
        metric = ('olla_bler' if case['id'] == 'pdsch_olla' else
                  next((m for m in ['bler_harq', 'bler', 'p_miss', 'uci_ber', 'preamble_error_rate', 'mse_pilots_db'] if m in metrics), metrics[0]))
        vals = []
        for snr in [snrs[0], snrs[-1]]:
            values = [r['value'] for r in records if r['case_id'] == case['id'] and r['metric'] == metric and r['snr_db'] == snr]
            value = (10 * math.log10(sum(10 ** (v / 10) for v in values) / len(values))
                     if metric.endswith('_db') else sum(values) / len(values))
            vals.append(f'{value:.5g}')
        lines.append(f'| {case["id"]} | {metric} | {vals[0]} | {vals[1]} |')
    lines += ['', '## 재현', '', '저장소 루트에서 실행:', '', '```bash',
              'python3 PHY/tests/run_awgn_campaign.py --output /tmp/awgn-repeat '
              f'--trials {n} --seeds ' + ' '.join(map(str, seeds)) +
              f' --snr-start {snrs[0]} --snr-end {snrs[-1]} --snr-step {manifest["snr_step"]}',
              '```', '',
              '[manifest.json](manifest.json)에 소스·바이너리 SHA-256, Git 상태, 실행별 설정/로그 SHA-256이 있다. '
              '변경 중인 작업 트리에서 측정했으므로 Git commit만으로 동일 소스를 식별할 수 없다. '
              'configs/와 logs/에 실행 입력과 원본 출력을 보존했다. 동일 플랫폼·바이너리·설정에서 재현한다.', '',
              '실행 경로 근거: `PHY/src/main.c`, `pbch.c`, `pdcch.c`, `pdsch.c`, `pusch.c`, `pucch.c`, '
              '`prach.c`, `csi_rs.c`, `srs.c`. 외부 공개 측정치를 혼합하지 않았다.', '']
    (out / 'SUMMARY.md').write_text('\n'.join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--trials', type=int, default=100)
    parser.add_argument('--seeds', type=int, nargs='+', default=[12345, 67890])
    parser.add_argument('--snr-start', type=int, default=-10)
    parser.add_argument('--snr-end', type=int, default=20)
    parser.add_argument('--snr-step', type=int, default=5)
    parser.add_argument('--case', action='append', dest='selected')
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    selected = cases()
    if args.selected:
        unknown = set(args.selected) - {c['id'] for c in selected}
        if unknown:
            parser.error(f'unknown cases: {sorted(unknown)}')
        selected = [c for c in selected if c['id'] in args.selected]
    if args.list:
        print('\n'.join(c['id'] for c in selected))
        return
    if args.output is None or args.trials < 1 or args.snr_step < 1 or args.snr_end < args.snr_start:
        parser.error('new --output directory and positive trials/valid SNR grid required')
    if len(set(args.seeds)) != len(args.seeds) or any(s < 1 or s > 2147483647 for s in args.seeds):
        parser.error('seeds must be distinct positive signed integers')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'configs').mkdir()
    (out / 'logs').mkdir()
    snrs = list(range(args.snr_start, args.snr_end + 1, args.snr_step))
    binary = PHY / 'lls_sim_c'
    subprocess.run(['make', '-f', 'c_Makefile', '-j4'], cwd=PHY, check=True)
    sources = sorted([*PHY.glob('src/*.c'), *PHY.glob('include/*.h'), PHY / 'c_Makefile', Path(__file__)])
    manifest = dict(status='running', created_utc=datetime.now(timezone.utc).isoformat(),
                    trials_per_seed_snr=args.trials, seeds=args.seeds, snr_db=snrs, snr_step=args.snr_step,
                    platform=platform.platform(), binary_sha256=sha(binary),
                    git_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=PHY, text=True).strip(),
                    git_status=subprocess.check_output(['git', 'status', '--short'], cwd=PHY, text=True),
                    source_sha256={str(p.relative_to(PHY)): sha(p) for p in sources}, cases=selected, runs=[])
    manifest_path = out / 'manifest.json'

    def save_manifest():
        manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')

    save_manifest()
    records = []
    for case in selected:
        print(f'Running {case["id"]}', flush=True)
        for seed in args.seeds:
            grids = [[s] for s in snrs] if case['id'] == 'pdsch_olla' else [snrs]
            for grid in grids:
                run_id = f'{case["id"]}_seed{seed}' + (f'_snr{grid[0]}' if len(grids) > 1 else '')
                cfg = dict(CHANNEL_MODEL='AWGN', NUM_RB=24, NFFT=1024, SCS_KHZ=30,
                           BANDWIDTH_MHZ=20, MIMO_MODE='SISO', CODING='LDPC', MCS_TABLE='TABLE1',
                           MCS_INDEX=5, EQUALIZER='MMSE', USE_DMRS=0, HARQ_ENABLE=0,
                           TRANSFORM_PRECODING=0, OLLA_ENABLE=0, IQ_DUMP=0,
                           NUM_TRIALS=args.trials, NUM_OFDM_SYMBOLS=args.trials,
                           SNR_START=grid[0], SNR_END=grid[-1], SNR_STEP=args.snr_step, SEED=seed)
                cfg.update(case['config'])
                config = out / 'configs' / f'{run_id}.cfg'
                log = out / 'logs' / f'{run_id}.txt'
                config.write_text(''.join(f'{k} = {v}\n' for k, v in cfg.items()))
                start = time.monotonic()
                try:
                    result = subprocess.run([str(binary), str(config)], cwd=PHY,
                                            capture_output=True, text=True, timeout=600)
                    log.write_text(result.stdout + result.stderr)
                    if result.returncode:
                        raise RuntimeError(f'exit {result.returncode}: {log}')
                    parsed = parse_output(result.stdout, case, grid)
                except Exception as exc:
                    manifest.update(status='failed', error=f'{run_id}: {exc}')
                    save_manifest()
                    raise
                for snr, metric, raw in parsed:
                    records.append(dict(case_id=case['id'], channel=case['channel'], seed=seed,
                                        snr_db=snr, trials=args.trials, metric=metric, value=float(raw),
                                        reported_value=raw, config=f'configs/{config.name}', log=f'logs/{log.name}'))
                manifest['runs'].append(dict(id=run_id, seconds=round(time.monotonic()-start, 3),
                                             config_sha256=sha(config), log_sha256=sha(log)))
                save_manifest()
    fields = ['case_id', 'channel', 'seed', 'snr_db', 'trials', 'metric', 'value', 'reported_value', 'config', 'log']
    with (out / 'results.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(records)
    manifest.update(status='complete', metric_rows=len(records), results_sha256=sha(out / 'results.csv'))
    write_summary(out, manifest, records)
    save_manifest()
    print(f'Complete: {len(selected)} cases, {len(manifest["runs"])} runs, {len(records)} metric rows: {out}')


if __name__ == '__main__':
    main()

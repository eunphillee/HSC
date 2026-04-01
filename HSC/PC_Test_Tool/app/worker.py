"""
Worker thread: runs Mainboard Modbus read/write so UI does not block.
"""
import time
from PyQt6.QtCore import QObject, QThread, pyqtSignal, Qt


class MainboardWorker(QObject):
    """Runs in a separate thread; performs Mainboard Modbus I/O and emits results."""
    di_result = pyqtSignal(bool, object, object, object, object)   # ok, bits[8], relay_states[4], vbits[4], err
    pc_led_result = pyqtSignal(bool, object, object)  # ok, state, err
    env_result = pyqtSignal(bool, object, object)     # ok, (temp_c, rh_pct), err
    write_result = pyqtSignal(bool, object)       # ok, err
    raw_bytes_received = pyqtSignal(list)          # raw RX bytes (e.g. 0xAA from board)
    sniff_result = pyqtSignal(list)                # 연결 직후 2초 수신 테스트 결과 (바이트 리스트)
    # HPSB/LPSB: ok, sense[40], coil_bits[14], error_flags u16, raw_blocks(dict) or None, err
    sub_data_result = pyqtSignal(bool, object, object, object, object, object)
    # Direct LPSB: FC04 start=0 count=14 (Unified Rule v1.2 LPSB map reg0..13)
    lpsb_adc_result = pyqtSignal(bool, object, object)  # ok, regs[14] or None, err
    # Direct HPSB: FC04 start=0 count=16 (Unified Rule v1.1 HPSB map reg0..15)
    hpsb_adc_result = pyqtSignal(bool, object, object)  # ok, regs[16] or None, err
    # 문서 기반 Modbus 테스트 탭 결과 (worker thread 경유)
    doc_fc01_result = pyqtSignal(bool, object, object)  # ok, bits or None, err
    doc_fc02_result = pyqtSignal(bool, object, object)  # ok, bits or None, err
    doc_fc04_result = pyqtSignal(bool, object, object)  # ok, regs or None, err
    doc_fc05_result = pyqtSignal(bool, object)          # ok, err
    doc_fc15_result = pyqtSignal(bool, object)          # ok, err
    rtc_result = pyqtSignal(bool, object, object)       # ok, regs[7] or None, err

    def __init__(self, client):
        super().__init__()
        self._client = client

    def on_request_read_di(self):
        if not self._client.connected:
            self.di_result.emit(False, None, None, None, "Not connected")
            return
        ok, bits, relay_states, vbits, err = self._client.read_di_bitmap()
        self.di_result.emit(ok, bits, relay_states, vbits, err)

    def on_request_read_pc_led(self):
        if not self._client.connected:
            self.pc_led_result.emit(False, None, "Not connected")
            return
        ok, state, err = self._client.read_pc_led_in()
        self.pc_led_result.emit(ok, state, err)

    def on_request_read_env(self):
        if not self._client.connected:
            self.env_result.emit(False, None, "Not connected")
            return
        ok, val, err = self._client.read_env_shtc3()
        self.env_result.emit(ok, val, err)

    def on_request_read_rtc(self):
        if not self._client.connected:
            self.rtc_result.emit(False, None, "Not connected")
            return
        ok, regs, err = self._client.read_board_time()
        self.rtc_result.emit(ok, regs, err)

    def on_request_set_rtc(self):
        import datetime
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        now = datetime.datetime.now()
        # Python weekday(): 0=Mon..6=Sun → Board: 0=Sun,1=Mon..6=Sat
        board_weekday = (now.weekday() + 1) % 7
        values = [now.year, now.month, now.day, board_weekday,
                  now.hour, now.minute, now.second]
        ok, err = self._client.write_board_time(values)
        self.write_result.emit(ok, err)

    def on_request_write_relay(self, ch: int, onoff: bool):
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_relay(ch, onoff)
        self.write_result.emit(ok, err)

    def on_request_write_virtual_bit(self, ch: int, onoff: bool):
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_mb_virtual_bit(ch, onoff)
        self.write_result.emit(ok, err)

    def on_request_write_pc_on_en(self, onoff: bool):
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_pc_on_en(onoff)
        self.write_result.emit(ok, err)

    def on_request_write_pc_reset_en(self, onoff: bool):
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_pc_reset_en(onoff)
        self.write_result.emit(ok, err)

    def on_request_pc_on_pulse(self):
        """PC_ON_EN: FC05 coil4 value=1 → 보드에서 500ms HIGH 펄스 수행."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_pc_on_en(True)
        self.write_result.emit(ok, err)

    def on_request_pc_reset_pulse(self):
        """PC_RESET_EN: FC05 coil6 value=1 → 보드에서 500ms HIGH 펄스 수행."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_pc_reset_en(True)
        self.write_result.emit(ok, err)

    def on_request_read_raw(self):
        """Read raw bytes from serial (e.g. board TX 0xAA) and emit for log."""
        if not self._client.connected:
            return
        buf = self._client.read_raw_available()
        if buf:
            self.raw_bytes_received.emit(buf)

    def on_request_sniff(self):
        """직접 HPSB 연결 직후 2초 대기 수신 테스트. 포트에 데이터가 오는지 확인."""
        if not self._client.connected:
            return
        buf = self._client.sniff_raw(timeout_sec=2.0)
        self.sniff_result.emit(buf)

    def on_request_read_sub(self):
        """Read HPSB/LPSB: sense(40), coil status(14), error_flags + raw blocks. Emit sub_data_result."""
        if not self._client.connected:
            self.sub_data_result.emit(False, None, None, None, None, "Not connected")
            return
        ok_s, sense, err_s = self._client.read_sub_sense()
        raw = self._client.get_last_sub_raw_copy() if ok_s else None
        ok_c, coils, err_c = self._client.read_sub_coil_status()
        ok_f, flags, err_f = self._client.read_error_flags()
        if not ok_s:
            self.sub_data_result.emit(False, None, None, None, None, err_s or "sense fail")
            return
        if not ok_c:
            self.sub_data_result.emit(False, sense, None, (flags if ok_f else None), raw, err_c or "coil fail")
            return
        self.sub_data_result.emit(True, sense, coils, (flags if ok_f else 0), raw, None)

    def on_request_read_direct_hpsb_adc(self):
        """HPSB 다이렉트: FC04 slave_id=1, start=0, count=16 (Unified Rule v1.1 HPSB map)."""
        if not self._client.connected:
            self.hpsb_adc_result.emit(False, None, "Not connected")
            return
        try:
            ok, result, err = self._client.read_input_registers(0, 16, unit=1)
        except Exception as e:
            self.hpsb_adc_result.emit(False, None, f"[FC04] {type(e).__name__}: {e}")
            return
        self.hpsb_adc_result.emit(ok, result, err)

    def on_request_sub_pulse(self, coil_index: int):
        """FC05 pulse one LPSB VB (coil_index 0..4 = VB 8..12). Emit write_result."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_sub_coil_pulse(coil_index)
        self.write_result.emit(ok, err)

    def on_request_write_sub_coil(self, addr: int, value: bool):
        """FC05 write sub-board coil (addr 899..910). Mainboard forwards to HPSB/LPSB. Emit write_result."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_sub_coil(addr, value)
        self.write_result.emit(ok, err)

    def on_request_write_direct_hpsb_coil(self, coil_index: int, value: bool):
        """HPSB 다이렉트: FC05 slave_id=1, coil_addr=0,1,2 (RELAY1,2,3). 메인보드 경유 없음."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        if coil_index < 0 or coil_index > 2:
            self.write_result.emit(False, "coil_index 0..2 only")
            return
        ok, err = self._client.write_coil_direct(1, coil_index, value)
        self.write_result.emit(ok, err)

    def on_request_write_direct_lpsb_coil(self, coil_index: int, value: bool):
        """LPSB 다이렉트: FC05 slave_id=2, coil_addr=0,1,2 (SSR1,2,3). 메인보드 경유 없음."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        if coil_index < 0 or coil_index > 2:
            self.write_result.emit(False, "coil_index 0..2 only")
            return
        ok, err = self._client.write_coil_direct(2, coil_index, value)
        self.write_result.emit(ok, err)

    def on_request_read_direct_lpsb_adc(self):
        """LPSB 다이렉트: FC04 slave_id=2, start=0, count=14 (Unified Rule FC04 map)."""
        if not self._client.connected:
            self.lpsb_adc_result.emit(False, None, "Not connected")
            return
        try:
            ok, result, err = self._client.read_input_registers(0, 14, unit=2)
        except Exception as e:
            self.lpsb_adc_result.emit(False, None, f"[FC04] {type(e).__name__}: {e}")
            return
        if not ok:
            self.lpsb_adc_result.emit(False, None, f"[FC04] {err or 'read fail'}")
            return
        self.lpsb_adc_result.emit(True, result, None)

    def on_request_diagnostic_sequence(self):
        """
        Run LED diagnostic sequence: HPSB RELAY1/2/3 ON then OFF, then LPSB1 SSR1/2/3 ON then OFF.
        Each step sends FC05 via Mainboard; observe LED2/3/4 on HPSB and LPSB to verify communication.
        Debug-first: simplest for visual verification, not production-polish.
        """
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        # (addr, value): 898..900 = HPSB coil 0,1,2; 901..909 = LPSB (match Mainboard h2_dec = start_addr+1)
        steps = [
            (898, True), (898, False),   # HPSB RELAY1 -> LED2
            (899, True), (899, False),   # HPSB RELAY2 -> LED3
            (900, True), (900, False),   # HPSB RELAY3 -> LED4
            (901, True), (901, False),   # LPSB1 SSR1
            (902, True), (902, False),   # LPSB1 SSR2
            (903, True), (903, False),   # LPSB1 SSR3
        ]
        delay_on_s = 1.2   # time to see LED on
        delay_off_s = 0.6  # pause before next
        for addr, value in steps:
            ok, err = self._client.write_sub_coil(addr, value)
            self.write_result.emit(ok, err if not ok else None)
            time.sleep(delay_on_s if value else delay_off_s)
        self.write_result.emit(True, "Diagnostic sequence done")

    # ---- Doc Modbus tab (Mainboard only) ----
    def on_request_doc_fc01(self, unit: int, start: int, count: int):
        """FC01: Unified Rule v1.2 금지 FC. 전송하지 않음."""
        del unit, start, count
        self.doc_fc01_result.emit(False, None, "FC01 not allowed (Unified Rule v1.2: FC04 read only)")

    def on_request_doc_fc02(self, unit: int, start: int, count: int):
        """FC02 (read_discrete_inputs): Unified Rule v1.2 금지 FC. 전송하지 않음."""
        del unit, start, count
        self.doc_fc02_result.emit(False, None, "FC02 not allowed (Unified Rule v1.2: FC04 read only)")

    def on_request_doc_fc04(self, unit: int, start: int, count: int):
        if not self._client.connected:
            self.doc_fc04_result.emit(False, None, "Not connected")
            return
        ok, regs, err = self._client.read_input_registers(start, count, unit=unit)
        self.doc_fc04_result.emit(ok, regs, err)

    def on_request_doc_fc05(self, unit: int, addr: int, value: bool):
        if not self._client.connected:
            self.doc_fc05_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_single_coil(addr, value, unit=unit)
        self.doc_fc05_result.emit(ok, err)

    def on_request_doc_fc15(self, unit: int, start: int, values: object):
        """FC15: Unified Rule v1.2 금지 FC. 전송하지 않음."""
        del unit, start, values
        self.doc_fc15_result.emit(False, "FC15 not allowed (Unified Rule v1.2: FC05 write only)")


def create_worker_and_thread(client):
    """Create worker in a new thread; caller must start the thread."""
    thread = QThread()
    worker = MainboardWorker(client)
    worker.moveToThread(thread)
    return thread, worker

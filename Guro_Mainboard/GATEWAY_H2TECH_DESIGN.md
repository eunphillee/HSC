# H2TECH Gateway Mapping Layer – Design Document

**Authority:** H2TECH 스마트 쉘터 매핑 PDF, MODBUS 프로토콜 PDF  
**Scope:** MAIN board only. Downstream (HPSB/LPSB) keeps existing 0-based internal mapping.

---

## 1. High-Level Gateway Design

```
   PC  ←── Modbus RTU (H2TECH absolute addresses) ──→  MAIN BOARD
                                                           │
                                    ┌──────────────────────┼──────────────────────┐
                                    │  Gateway              │  Downstream           │
                                    │  - H2TECH addr table  │  - Modbus Master      │
                                    │  - Translate 1x/0x/  │  - Poll HPSB/LPSB     │
                                    │    3x/4x → internal  │  - 0-based internal   │
                                    │  - Aggregated image  │  - No change          │
                                    └──────────────────────┴──────────────────────┘
```

- **Upstream (PC → MAIN):** MAIN acts as **Modbus Slave**. PC sends requests with H2TECH addresses (e.g. start address 821, 869, 897). MAIN uses **translation table** to resolve to internal source (local / HPSB / LPSB) and internal address, then composes response from **aggregated image** or forwards to downstream.
- **Downstream (MAIN → HPSB/LPSB):** Unchanged. MAIN remains Modbus Master, polls with 0-based addresses.
- **No BASE subtraction:** Addresses (821, 869, 897, …) are **absolute logical keys**. Each entry in the table is explicit: `H2TECH_addr_dec → { area_type, rw, source, slave_id, internal_type, internal_addr, bit_index/reg_index }`.

---

## 2. Address Translation Table Structure

Every H2TECH address used in the mapping document is one row in the translation table.

### 2.1 Area Type (Modbus)

| Symbol | Meaning        | FC (read) | FC (write) |
|--------|----------------|-----------|------------|
| 1x     | Discrete Input | FC02      | —          |
| 0x     | Coil           | FC01      | FC05/FC15  |
| 3x     | Input Register | FC04      | —          |
| 4x     | Holding Reg    | FC03      | FC06/FC16  |

### 2.2 Source

- **Main (local):** Data from MAIN’s own aggregated image (SHTC3, main DI/DO, or cached sub-board data).
- **HPSB:** Data from Modbus poll of Slave 1 (internal 0-based coil/discrete/holding/input).
- **LPSB:** Data from Modbus poll of Slave 2 (internal 0-based).

### 2.3 Entry Descriptor (C struct)

```c
typedef enum {
    H2TECH_AREA_DISCRETE = 2,   /* 1x */
    H2TECH_AREA_COIL     = 0,   /* 0x */
    H2TECH_AREA_INPUT_REG= 3,   /* 3x */
    H2TECH_AREA_HOLDING  = 4    /* 4x */
} h2tech_area_t;

typedef enum {
    H2TECH_SOURCE_MAIN,
    H2TECH_SOURCE_HPSB,
    H2TECH_SOURCE_LPSB
} h2tech_source_t;

typedef struct {
    uint16_t    h2tech_addr;      /* Absolute logical address (e.g. 821, 869) */
    h2tech_area_t area;          /* 0x/1x/3x/4x */
    uint8_t     rw;              /* 0=RO, 1=RW */
    h2tech_source_t source;
    uint8_t     slave_id;        /* 0=main, 1=HPSB, 2=LPSB */
    uint8_t     internal_type;   /* 0=coil, 1=discrete, 2=holding, 3=input_reg */
    uint16_t    internal_addr;   /* 0-based internal address */
    uint8_t     bit_index;       /* 0..15 for bit within word; 0xFF = whole reg/byte */
} h2tech_map_entry_t;
```

- **Bit packing:** LSB-first, 8 bits per byte, as per H2TECH protocol. For 1x/0x, `bit_index` is the bit number in the aggregated image byte/word; for 3x/4x, `bit_index == 0xFF` means full 16-bit register.

---

## 3. Data Model for Aggregated Image

The aggregated image on MAIN is the single source of truth for building upstream responses. It is filled by the existing **Aggregator** from:

- SHTC3 (temp, humidity)
- Main DI/DO
- HPSB coils, discrete, holding (status/alarm), current sensing
- LPSB coils, discrete, holding, sensing

Structure remains `aggregated_status_t`; H2TECH translation table maps **H2TECH address → offset or bit inside this struct** (or to a derived “logical” image that mirrors H2TECH layout). For example:

- 1x0821 ~ 1x0836 → bits in “HPSB discrete” image (or main DI if document says so).
- 3x/4x ranges → 16-bit values from aggregated_status (e.g. env_temp_cx10, env_rh_x10, hpsb_status_reg, …).

So:

- **Aggregated image:** `aggregated_status_t` (+ optional extra arrays for large H2TECH ranges).
- **Translation table:** For each H2TECH address, `internal_addr`/`bit_index` point into this image (e.g. “holding reg index 0”, “discrete byte 0, bit 5”).

---

## 4. Example: H2TECH 1x0821 → Internal

- **H2TECH:** 1x0821 = Discrete Input, logical address 821.
- **Table entry (example):**

  - `h2tech_addr = 821`
  - `area = H2TECH_AREA_DISCRETE (1x)`
  - `rw = 0` (read-only)
  - `source = H2TECH_SOURCE_HPSB`
  - `slave_id = 1`
  - `internal_type = 1` (discrete)
  - `internal_addr = 0` (first byte of HPSB discrete image)
  - `bit_index = 0` (LSB of that byte)

- **Resolution:** When PC requests FC02, start=821, count=1, MAIN returns the value of **HPSB discrete bit 0** from the aggregated image (already updated by downstream poll). So: **1x0821 → internal HPSB discrete, byte 0, bit 0**, LSB-first.

If the PDF assigns 1x0821 to another meaning (e.g. main DI 1), only the table entry is changed; the gateway logic stays the same.

---

## 5. Concrete Mapping List (Authoritative H2TECH Definition)

### 5.0 H2TECH 1x Blocks (from PDF)

- **SECTION 1 – DIGITAL OUTPUT STATUS (READ ONLY)**
  - Range: **1x0821 ~ 1x0836** (ON/OFF 1..16)
  - Type: 1x, Access: READ
- **SECTION 2 – DOOR OPEN SENSORS (READ ONLY)**
  - Range: **1x0853 ~ 1x0860** (자동문 열림 센서, 외부 문열림 스위치 1..4)
  - Type: 1x, Access: READ
- **SECTION 3 – ALARM STATUS (READ ONLY)**
  - Range: **1x0869 ~ 1x0880** (Alarm 1..12)
  - Type: 1x, Access: READ
- **SECTION 4 – VIRTUAL BUTTON / CONTROL (WRITE)**
  - Range: **1x0892 ~ 1x0900** (ON/OFF 8..12, 자동문 문열림 제어 1..4)
  - Type: 1x (논리 제어), Access: WRITE
  - “해당 버튼이 ON이 되야 자동문 작동” → 게이트웨이에서 **논리 버튼을 coil 제어(pulse/latch)** 로 변환.

**규칙:** 위에 나열된 주소 **이외**(예: 0881~0891)는 1x 영역 안에 있어도 모두 **0x02 (Illegal Data Address)** 로 처리한다.

### 5.1 구현된 매핑 (h2tech_address_map.c)

게이트웨이 내부에서는 단순화된 **게이트웨이용 이미지**에 매핑한다. 실제 하드웨어(HPSB/LPSB/MAIN DO/DI)와의 연결은 Aggregator / IO 레이어에서 담당한다.

| H2TECH Range (dec) | 의미 (PDF 기준)           | area (게이트웨이) | source | internal_type | internal_addr(Byte) | bit_index |
|--------------------|---------------------------|-------------------|--------|---------------|---------------------|-----------|
| 821..828           | ON/OFF 1..8 (DO status)   | DISCRETE (1x)     | MAIN   | discrete      | 0                   | 0..7      |
| 829..836           | ON/OFF 9..16 (DO status)  | DISCRETE (1x)     | MAIN   | discrete      | 1                   | 0..7      |
| 853..860           | Door sensors / switches   | DISCRETE (1x)     | MAIN   | discrete      | 2                   | 0..7      |
| 869..876           | Alarm 1..8                | DISCRETE (1x)     | MAIN   | discrete      | 3                   | 0..7      |
| 877..880           | Alarm 9..12               | DISCRETE (1x)     | MAIN   | discrete      | 4                   | 0..3      |
| 892..899           | ON/OFF8..12 + 문열림 1..3 | COIL (논리 버튼)  | MAIN   | coil          | 0                   | 0..7      |
| 900                | 문열림 제어 4             | COIL (논리 버튼)  | MAIN   | coil          | 1                   | 0         |

- 각 H2TECH 주소는 위 테이블과 같이 **개별 row**로 `h2tech_map_entry_t`에 들어간다.
- SECTION 4(0892~0900)는 H2TECH 문서상 1x이지만, 쓰기 기능(FC05/15)을 위해 내부적으로 **COIL 영역**으로 매핑하고, 게이트웨이가 논리 버튼을 실제 제어(pulse/latch)로 변환한다.

### 5.1 Implemented Mapping (h2tech_address_map.c)

| H2TECH (dec) | Area | Source | internal_type | internal_addr | bit_index |
|--------------|------|--------|---------------|----------------|-----------|
| 821..828     | 1x   | HPSB   | discrete      | 0              | 0..7      |
| 829..836     | 1x   | HPSB   | discrete      | 1              | 0..7      |
| 853..860     | 1x   | MAIN   | discrete      | 0              | 0..7      |
| 869..876     | 1x   | LPSB   | discrete      | 0              | 0..7      |
| 877..880     | 1x   | LPSB   | discrete      | 1              | 0..3      |
| 885..892     | 1x   | MAIN   | discrete      | 1              | 0..7      |
| 893..900     | 1x   | MAIN   | discrete      | 2,3            | 0..7      |

Replace with exact PDF definitions when available.

---

## 6. Upstream Modbus Slave Handler Skeleton

- **Role:** On MAIN, one Modbus Slave instance over **USART2** (PC link). Receives requests with **H2TECH addresses** (e.g. start 821, count 16).
- **Flow:**
  1. Receive frame (already in place with ReceiveToIdle_IT).
  2. Parse FC and start address + count.
  3. For each requested address in [start, start+count):
     - Look up `h2tech_addr` in the translation table.
     - If **not found** → return **Modbus exception 0x02 (Illegal Data Address)**.
     - If found:
       - Determine area (1x/0x/3x/4x) and check FC matches (e.g. FC02 for 1x).
       - Read value from aggregated image using `source`, `internal_type`, `internal_addr`, `bit_index`.
  4. Compose response (FC01/02/03/04 with LSB-first bit packing for 0x/1x; high byte first for 3x/4x) and send.
- **Writes (FC05/06/15/16):** Same lookup. If `rw == 0` → exception. Else write into aggregated image (or queue command to downstream). For downstream targets, MAIN may need to issue a downstream Modbus write and then update local image when convenient.

Skeleton (pseudo):

```c
void UpstreamSlave_OnRequest(uint8_t fc, uint16_t start_addr, uint16_t count, uint8_t *write_data) {
    if (fc == FC02) {
        for (uint16_t i = 0; i < count; i++) {
            const h2tech_map_entry_t *e = H2TechMap_Lookup(H2TECH_AREA_DISCRETE, start_addr + i);
            if (!e) { send_exception(0x02); return; }
            bit = AggregatedImage_GetDiscrete(e);  // from aggregated_status
            pack_lsb(bit, response_bytes);
        }
        send_response(FC02, response_bytes);
    }
    // FC01, FC03, FC04 similarly; FC05/06/15/16 for writes with rw check.
}
```

### 6.1 Skeleton Implementation Notes

- **FC → area:** FC01 → coil, FC02 → discrete, FC03 → holding, FC04 → input_reg. Validate FC before lookup.
- **Range check:** Before building response, call `H2TechMap_IsRangeDefined(area, start_addr, count)`. If 0, send exception 0x02 immediately.
- **Exception response frame (Modbus):** `[slave_id][FC|0x80][exception_code][CRC]`. Example: exception 0x02 → `[id][0x82][0x02][CRC]` for FC02.
- **Read path:** For each address in range, lookup → get byte/reg from aggregated image by (source, internal_type, internal_addr), then extract bit by `bit_index` if not 0xFF. Pack LSB-first for coils/discrete; high byte first for 3x/4x.
- **Write path:** Lookup each address; if `rw == 0` send 0x03 (Illegal Data Value) or 0x02 per protocol; else write to aggregated image and/or queue downstream write.

---

## 7. Exception Handling (H2TECH Protocol)

- **Exception 0x02 (Illegal Data Address):** Returned when the requested start address (or any address in range) is **not present** in the H2TECH translation table.
- **Exception 0x01 (Illegal Function):** If FC is not supported.
- **Exception 0x03 (Illegal Data Value):** If count or register value is invalid per document.

Implementation: after parsing request, iterate requested addresses; if any `H2TechMap_Lookup(area, addr)` returns NULL, abort and send exception response with exception code 0x02.

---

## 8. File Layout

- **Gateway/**
  - `Inc/h2tech_address_map.h` – Translation table descriptor, lookup API, exception codes.
  - `Src/h2tech_address_map.c` – Table (1x0821~0836, 0853~0860, 0869~0880, 0892~0900), `H2TechMap_Lookup()`, `H2TechMap_IsRangeDefined()`, `H2TechMap_EntryCount()`.
  - `Inc/upstream_slave_h2tech.h`, `Src/upstream_slave_h2tech.c` – Upstream Modbus Slave handler skeleton: range validation, exception 0x01/0x02/0x03 response.
- **Upstream Slave** (integration):
  - Uses `h2tech_address_map` and aggregated_status / gateway images to build responses; sends exception 0x02 for any undefined H2TECH address.

---

## 9. Summary

- **No redesign of address system:** Internal 0-based mapping for HPSB/LPSB unchanged.
- **Explicit translation:** Every H2TECH address used in the document is an explicit row: `h2tech_addr → { area, rw, source, slave_id, internal_type, internal_addr, bit_index }`.
- **MAIN upstream:** Modbus Slave on USART2, H2TECH addresses only; responses built from aggregated image via translation table; undefined address → 0x02.
- **Concrete list:** Table includes at least 1x0821~0836, 1x0853~0860, 1x0869~0880, 1x0885~0900; exact mapping to internal source/bit/reg to be copied from H2TECH 매핑 PDF when available.

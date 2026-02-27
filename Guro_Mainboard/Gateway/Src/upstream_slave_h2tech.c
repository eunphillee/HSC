/**
 * @file upstream_slave_h2tech.c
 * @brief Skeleton: upstream Modbus Slave handler using H2TECH translation table.
 *        Validates address range -> return exception 0x02 if any address undefined.
 *        Full read/write implementation to be completed with AggregatedImage_Get*/Set*.
 */
#include "upstream_slave_h2tech.h"
#include "h2tech_address_map.h"

/* Map Modbus FC to H2TECH area for reads */
static h2tech_area_t fc_to_area_read(uint8_t fc)
{
    switch (fc) {
        case 0x01: return H2TECH_AREA_COIL;
        case 0x02: return H2TECH_AREA_DISCRETE;
        case 0x03: return H2TECH_AREA_HOLDING;
        case 0x04: return H2TECH_AREA_INPUT_REG;
        default:   return (h2tech_area_t)0xFF;
    }
}

int UpstreamSlave_HandleRequest(uint8_t fc, uint16_t start_addr, uint16_t count,
                                const uint8_t *write_data,
                                const void *p_agg,
                                uint8_t *response, uint16_t resp_max)
{
    if (!response || resp_max < 2u) return -1;

    /* Read requests: validate range via translation table */
    if (fc >= 0x01 && fc <= 0x04) {
        h2tech_area_t area = fc_to_area_read(fc);
        if ((uint8_t)area == 0xFF) {
            response[0] = fc | 0x80;
            response[1] = H2TECH_EX_ILLEGAL_FUNCTION;
            return 2;
        }
        if (!H2TechMap_IsRangeDefined(area, start_addr, count)) {
            response[0] = fc | 0x80;
            response[1] = H2TECH_EX_ILLEGAL_DATA_ADDRESS;
            return 2;
        }
        /* TODO: Build response from aggregated image using H2TechMap_Lookup per address.
         * For FC01/02: pack bits LSB-first into response bytes.
         * For FC03/04: pack 16-bit regs high byte first.
         * Example for FC02:
         *   for (i=0; i<count; i++) {
         *     const h2tech_map_entry_t *e = H2TechMap_Lookup(area, start_addr + i);
         *     uint8_t byte_idx = i/8, bit_idx = i%8;
         *     uint8_t bit = AggregatedImage_GetDiscreteBit(p_agg, e);
         *     if (bit) response[2+byte_idx] |= (1u << bit_idx);
         *   }
         * return 2 + byte_count;
         */
        (void)p_agg;
        (void)count;
        (void)start_addr;
        return 0; /* no response body implemented yet */
    }

    /* Write requests: FC05/06/15/16 - validate each address and rw */
    if (fc == 0x05 || fc == 0x06 || fc == 0x15 || fc == 0x16) {
        h2tech_area_t area = (fc == 0x05 || fc == 0x15) ? H2TECH_AREA_COIL : H2TECH_AREA_HOLDING;
        if (!H2TechMap_IsRangeDefined(area, start_addr, count)) {
            response[0] = fc | 0x80;
            response[1] = H2TECH_EX_ILLEGAL_DATA_ADDRESS;
            return 2;
        }
        /* TODO: For each address, lookup; if e->rw == 0 return 0x03; else write to image / queue downstream. */
        (void)write_data;
        (void)p_agg;
        return 0;
    }

    response[0] = fc | 0x80;
    response[1] = H2TECH_EX_ILLEGAL_FUNCTION;
    return 2;
}

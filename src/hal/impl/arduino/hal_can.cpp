#include "../../hal_config.h"
#ifndef HAL_DISABLE_CAN

#include "../../hal_can.h"
#include "../../hal_config.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include <SPI.h>
#include "drivers/MCP2515/mcp_can.h"
#include <string.h>
#include <new>

struct hal_can_impl_s {
    MCP_CAN     *mcp;
    int          in_use;
    hal_mutex_t  mutex;
};

static hal_can_impl_t s_pool[HAL_CAN_MAX_INSTANCES];
static uint8_t s_mcp_mem[HAL_CAN_MAX_INSTANCES][sizeof(MCP_CAN)]
    __attribute__((aligned(__alignof__(MCP_CAN))));

hal_can_t hal_can_create(uint8_t cs_pin) {
    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < hal_get_config()->can_max_instances; i++) {
        if (!s_pool[i].in_use) { slot = i; s_pool[slot].in_use = 1; break; }
    }
    hal_critical_section_exit();

    if (slot < 0) {
        HAL_ASSERT(0, "hal_can: pool exhausted – increase HAL_CAN_MAX_INSTANCES");
        return NULL;
    }

    hal_can_impl_t *h = &s_pool[slot];
    h->mutex = hal_mutex_create();
    h->mcp   = new(s_mcp_mem[slot]) MCP_CAN(cs_pin);
    if (h->mcp->begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
        h->mcp->~MCP_CAN();
        hal_mutex_destroy(h->mutex);
        h->mutex  = NULL;
        h->in_use = 0;
        return NULL;
    }
    h->mcp->setMode(MCP_NORMAL);
    h->mcp->enOneShotTX();
    h->mcp->setSleepWakeup(1);
    return h;
}

void hal_can_destroy(hal_can_t h) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->mcp->~MCP_CAN();
    h->in_use = 0;
    hal_mutex_unlock(h->mutex);
}

bool hal_can_send(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data) {
    if (!h || !h->in_use) {
        hal_derr_limited("can", "send called with NULL handle");
        return false;
    }
    if (len > 0 && data == NULL) {
        hal_derr_limited("can", "send called with NULL data pointer and len=%u", (unsigned)len);
        return false;
    }
    uint8_t buf[HAL_CAN_MAX_DATA_LEN];
    uint8_t safe_len = len <= HAL_CAN_MAX_DATA_LEN ? len : HAL_CAN_MAX_DATA_LEN;
    if (safe_len > 0) {
        memcpy(buf, data, safe_len);
    }
    hal_mutex_lock(h->mutex);
    bool ok = h->mcp->sendMsgBuf(id, safe_len, buf) == CAN_OK;
    hal_mutex_unlock(h->mutex);
    if(!ok){
        hal_derr_limited("can", "send failed for id=%u", (unsigned)id);
    }
    return ok;
}

bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data) {
    if (!h || !h->in_use) {
        hal_derr_limited("can", "receive called with NULL handle");
        return false;
    }
    if (!id || !len || !data) {
        hal_derr_limited("can", "receive called with NULL output pointer(s)");
        return false;
    }
    hal_mutex_lock(h->mutex);
    if (h->mcp->checkReceive() != CAN_MSGAVAIL) {
        hal_mutex_unlock(h->mutex);
        return false;
    }
    byte buf[HAL_CAN_MAX_DATA_LEN] = {};
    byte msg_len = 0;
    long unsigned int msg_id = 0;
    bool ok = h->mcp->readMsgBuf(&msg_id, &msg_len, buf) == CAN_OK;
    hal_mutex_unlock(h->mutex);
    if (!ok) return false;
    *id  = (uint32_t)msg_id;
    *len = msg_len;
    memcpy(data, buf, msg_len < HAL_CAN_MAX_DATA_LEN ? msg_len : HAL_CAN_MAX_DATA_LEN);
    return true;
}

bool hal_can_available(hal_can_t h) {
    if (!h || !h->in_use) return false;
    hal_mutex_lock(h->mutex);
    bool v = h->mcp->checkReceive() == CAN_MSGAVAIL;
    hal_mutex_unlock(h->mutex);
    return v;
}

bool hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1) {
    if (!h || !h->in_use) return false;
    hal_mutex_lock(h->mutex);
    // MCP_CAN library expects standard IDs left-shifted by 16 with ext=0.
    uint32_t mask = 0x07FF0000UL;           // match all 11 bits
    uint32_t fid0 = (id0 & 0x7FFU) << 16;
    uint32_t fid1 = (id1 & 0x7FFU) << 16;
    bool ok = true;
    ok = ok && (h->mcp->init_Mask(0, 0, mask) == CAN_OK); // buffer-0 group
    ok = ok && (h->mcp->init_Mask(1, 0, mask) == CAN_OK); // buffer-1 group
    ok = ok && (h->mcp->init_Filt(0, 0, fid0) == CAN_OK); // RXB0 filter 0
    ok = ok && (h->mcp->init_Filt(1, 0, fid1) == CAN_OK); // RXB0 filter 1
    ok = ok && (h->mcp->init_Filt(2, 0, fid0) == CAN_OK); // RXB1 filter 2
    ok = ok && (h->mcp->init_Filt(3, 0, fid1) == CAN_OK); // RXB1 filter 3
    ok = ok && (h->mcp->init_Filt(4, 0, fid0) == CAN_OK); // RXB1 filter 4
    ok = ok && (h->mcp->init_Filt(5, 0, fid1) == CAN_OK); // RXB1 filter 5
    hal_mutex_unlock(h->mutex);
    return ok;
}

#endif /* HAL_DISABLE_CAN */

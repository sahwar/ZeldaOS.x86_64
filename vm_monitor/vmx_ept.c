/*
 * Copyright (c) 2019 Jie Zheng
 */

#include <vm_monitor/include/vmx_ept.h>
#include <lib64/include/logging.h>
#include <memory/include/paging.h>
#include <lib64/include/string.h>

uint64_t
guestpa_to_hostpa(uint64_t ept_base, uint64_t guestpa)
{
    uint64_t hostpa = -1;
    int l4_index = LEVEL_4_INDEX(guestpa);
    int l3_index = LEVEL_3_INDEX(guestpa);
    int l2_index = LEVEL_2_INDEX(guestpa);
    int l1_index = LEVEL_1_INDEX(guestpa);
    struct ept_pml4e * _pml4e = l4_index + (struct ept_pml4e *)ept_base;
    if (!_pml4e->read_access && !_pml4e->write_access) {
        goto out;
    }

    struct ept_pdpe * _pdpe =
        l3_index + (struct ept_pdpe *)(uint64_t)(_pml4e->ept_pdpt << PAGE_SHIFT_4K);
    if (!_pdpe->read_access && !_pdpe->write_access) {
        goto out;
    }

    struct ept_pde * _pde =
        l2_index + (struct ept_pde *)(uint64_t)(_pdpe->ept_pdt << PAGE_SHIFT_4K);
    if (!_pde->read_access && !_pde->write_access) {
        goto out;
    }

    struct ept_pte * _pt =
        l1_index + (struct ept_pte *)(uint64_t)(_pde->ept_pt << PAGE_SHIFT_4K);
    if (!_pt->read_access && _pt->write_access) {
        goto out;
    }
    hostpa = _pt->ept_4k_page << PAGE_SHIFT_4K;
    hostpa |= guestpa & PAGE_MASK_4K;

    out:
        return hostpa;
}


static int
map_ept_page(uint64_t base, uint64_t guest_pa, uint64_t phy_pa, int backing)
{
    int l4_index = LEVEL_4_INDEX(guest_pa);
    int l3_index = LEVEL_3_INDEX(guest_pa);
    int l2_index = LEVEL_2_INDEX(guest_pa);
    int l1_index = LEVEL_1_INDEX(guest_pa);

    struct ept_pml4e * _pml4e = l4_index + (struct ept_pml4e *)base; 
    if (!_pml4e->read_access && !_pml4e->write_access) {
        memset(_pml4e, 0x0, sizeof(struct ept_pml4e));
        _pml4e->read_access = 1;
        _pml4e->write_access = 1;
        _pml4e->instruction_fetchable = 1;
        _pml4e->usermode_instruction_fetchable = 1;
        uint64_t _ept_pdpt = get_physical_page();
        ASSERT(_ept_pdpt && pa(_ept_pdpt) == _ept_pdpt);
        memset((void *)_ept_pdpt, 0x0, PAGE_SIZE_4K);
        _pml4e->ept_pdpt = _ept_pdpt >> PAGE_SHIFT_4K;
    }
    ASSERT(_pml4e->read_access || _pml4e->write_access);

    struct ept_pdpe * _pdpe =
        l3_index + (struct ept_pdpe *)(uint64_t)(_pml4e->ept_pdpt << PAGE_SHIFT_4K);
    if (!_pdpe->read_access && !_pdpe->write_access) {
        memset(_pdpe, 0x0, sizeof(struct ept_pdpe));
        _pdpe->read_access = 1;
        _pdpe->write_access = 1;
        _pdpe->instruction_fetchable = 1;
        _pdpe->usermode_instruction_fetchable = 1;
        uint64_t _ept_pdt = get_physical_page();
        ASSERT(_ept_pdt && pa(_ept_pdt) == _ept_pdt);
        memset((void *)_ept_pdt, 0x0, PAGE_SIZE_4K);
        _pdpe->ept_pdt = _ept_pdt >> PAGE_SHIFT_4K;
    }
    ASSERT(_pdpe->read_access || _pdpe->write_access);

    struct ept_pde * _pde =
        l2_index + (struct ept_pde *)(uint64_t)(_pdpe->ept_pdt << PAGE_SHIFT_4K);
    if (!_pde->read_access && !_pde->write_access) {
        memset(_pde, 0x0, sizeof(struct ept_pde));
        _pde->read_access =1;
        _pde->write_access =1;
        _pde->instruction_fetchable = 1;
        _pde->usermode_instruction_fetchable = 1;
        uint64_t _ept_pt = get_physical_page();
        ASSERT(_ept_pt && pa(_ept_pt) == _ept_pt);
        memset((void *)_ept_pt, 0x0, PAGE_SIZE_4K);
        _pde->ept_pt = _ept_pt >> PAGE_SHIFT_4K;
    }
    ASSERT(_pde->read_access || _pde->write_access);

    struct ept_pte * _pt =
        l1_index + (struct ept_pte *)(uint64_t)(_pde->ept_pt << PAGE_SHIFT_4K);
    if (_pt->read_access || _pt->write_access) {
        return -ERROR_DUPLICATION;
    }
    memset(_pt, 0x0, sizeof(struct ept_pte));
    if (backing == PAGE_SYSTEM_MEMORY) {
        _pt->read_access = 1;
        _pt->write_access = 1;
        _pt->instruction_fetchable = 1;
        _pt->usermode_instruction_fetchable = 1;
        // FIXED: fill other fields of the last entry
        // See 28.2.6.2
        _pt->ignore_pat_memory_type = 1;
        _pt->ept_memory_type = 6;
        _pt->ept_4k_page = phy_pa >> PAGE_SHIFT_4K;
    } else if (backing == PAGE_SYSTEM_IOMEMORY) {
        // Mark it as mis-configured entry
        _pt->read_access = 0;
        _pt->write_access = 1;
        _pt->instruction_fetchable = 1;
        _pt->usermode_instruction_fetchable = 1;
        _pt->ignore_pat_memory_type = 1;
        _pt->ept_memory_type = 6;
    }
    return ERROR_OK;
}
uint64_t
setup_basic_physical_memory(uint64_t addr_low, uint64_t addr_high)
{
    uint64_t ept_pml4_base = get_physical_page();
    ASSERT(ept_pml4_base && pa(ept_pml4_base) == ept_pml4_base);
    memset((void *)ept_pml4_base, 0x0, PAGE_SIZE_4K);
    LOG_TRIVIA("new ept pml4 base:0x%x\n",ept_pml4_base);

    uint64_t guest_pa = addr_low;
    for (; guest_pa <= addr_high; guest_pa += PAGE_SIZE_4K) {
        uint64_t __pa = get_physical_page();
        ASSERT(__pa && pa(__pa) == __pa);
        memset((void *)__pa, 0x0, PAGE_SIZE_4K);
        map_ept_page(ept_pml4_base, guest_pa, __pa, PAGE_SYSTEM_MEMORY);
    }
    return ept_pml4_base;
}

void
setup_io_memory(uint64_t ept_pml4_base, uint64_t guest_physical_page)
{
    ASSERT(map_ept_page(ept_pml4_base, guest_physical_page, 0x0,
                        PAGE_SYSTEM_IOMEMORY) == ERROR_OK);

}

__attribute__((constructor)) static void
vmx_ept_pre_init(void)
{
    ASSERT(sizeof(struct ept_pml4e) == 8);
    ASSERT(sizeof(struct ept_pdpe) == 8);
    ASSERT(sizeof(struct ept_pde) == 8);
    ASSERT(sizeof(struct ept_pte) == 8);
}


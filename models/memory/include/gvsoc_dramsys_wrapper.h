#include "DRAMSys/DRAMSys.h"
#include "DRAMSys/common/PhysicalStorage.h"
#include "DRAMSys/configuration/json/DRAMSysConfiguration.h"
#include "DRAMSys/configuration/memspec/MemSpec.h"
#include "DRAMSys/simulation/AddressDecoder.h"
#include "DRAMSys/statistics/PrettyFormat.h"
#include "dramsys_conv.h"
#include <filesystem>


#define svOpenArrayHandle void *
#define DRAMSYS_RESOURCE_DIR "../../../../third_party/DRAMSys/configs/"

struct GvsocMemspec {
    uint access_size;
    uint nb_channels;
    uint nb_pseudo_channels;
    uint nb_ranks;
    uint nb_bank_groups;
    uint nb_banks;
    uint nb_rows;
    uint nb_columns;
    uint channel_stride;
    uint rank_stride;
    uint bankgroup_stride;
    uint bank_stride;
    uint row_stride;
    uint column_stride;
};

std::vector<uint64_t> list_of_DRAMburst;
std::vector<uint64_t> list_of_DRAMsize;
std::vector<DRAMSys::DRAMSys *> list_of_DRAMsys;
 std::unique_ptr<DRAMSys::PhysicalStorage> physicalStorage;
std::vector<dramsys_conv *> list_of_conv;
std::vector<uint8_t *> list_of_wbuffer;
std::vector<uint8_t *> list_of_wstrobe;

int add_dram(char *resources_path, char *simulationJson_path, GvsocMemspec *memspec) {

    static int id = 0;

    std::filesystem::path resourceDirectory = DRAMSYS_RESOURCE_DIR;
    if (resources_path != 0) {
        resourceDirectory = resources_path;
    }

    std::filesystem::path baseConfig = resourceDirectory / "hbm2-example.json";
    if (simulationJson_path != 0 && std::filesystem::exists(simulationJson_path)) {
        baseConfig = simulationJson_path;
    }

    DRAMSys::Config::Configuration configuration = DRAMSys::Config::from_path(baseConfig.c_str());

    DRAMSys::DRAMSys *dramSys;
    dramsys_conv *conv;

    std::string dramsys_name = "DRAMSysCtrl";
    std::string conv_name = "dramsys_conv";
    std::string id_str = std::to_string(id);
    dramsys_name = dramsys_name + id_str;
    conv_name = conv_name + id_str;

    dramSys = new DRAMSys::DRAMSys(dramsys_name.c_str(), configuration);
    physicalStorage = std::make_unique<DRAMSys::PhysicalStorage>(dramSys->memorySize());
    dramSys->setBackingStore(physicalStorage->data());
    conv = new dramsys_conv(conv_name.c_str());

    conv->iSocket.bind(dramSys->tSocket);
    // init systemC engine
    if (id == 0) {
        auto start = std::chrono::high_resolution_clock::now();
        sc_set_stop_mode(SC_STOP_FINISH_DELTA);
    }

    uint64_t dramChannelSize =
        dramSys->getMemSpec().getSimMemSizeInBytes() / dramSys->getMemSpec().numberOfChannels;
    uint64_t dramMaxBurstByte = dramSys->getMemSpec().maxBytesPerBurst;

    // put them into vector container
    list_of_DRAMsys.push_back(dramSys);
    list_of_conv.push_back(conv);
    list_of_DRAMsize.push_back(dramChannelSize);
    list_of_DRAMburst.push_back(dramMaxBurstByte);

    uint8_t *wbuffer_ptr = new uint8_t[2048];
    list_of_wbuffer.push_back(wbuffer_ptr);

    uint8_t *wstrobe_ptr = new uint8_t[2048];
    list_of_wstrobe.push_back(wstrobe_ptr);

    std::cout << "the instantiated DRAM id is: " << id << std::endl;

    uint64_t addr = 0;
    uint8_t *wbuf_ptr = list_of_wbuffer[0];
    uint8_t *wstrb_ptr = list_of_wstrobe[0];

    // Fill the memspec struct to return to the caller
    if (memspec != nullptr) {
        memspec->access_size = dramMaxBurstByte;
        memspec->nb_channels = dramSys->getMemSpec().numberOfChannels;
        memspec->nb_ranks = dramSys->getMemSpec().ranksPerChannel;
        if (dramSys->getMemSpec().pseudoChannelMode()) {
            memspec->nb_pseudo_channels =
                dramSys->getMemSpec().ranksPerChannel; // HBM overlays pseudo channel with rank
            memspec->nb_bank_groups =
                dramSys->getMemSpec().groupsPerRank / dramSys->getMemSpec().ranksPerChannel;
        } else {
            memspec->nb_pseudo_channels = 1;
            memspec->nb_bank_groups = dramSys->getMemSpec().groupsPerRank;
        }
        memspec->nb_banks = dramSys->getMemSpec().banksPerGroup;
        memspec->nb_rows = dramSys->getMemSpec().rowsPerBank;
        memspec->nb_columns = dramSys->getMemSpec().columnsPerRow;

        memspec->channel_stride = dramSys->getAddressDecoder().encodeAddress(
            DRAMSys::DecodedAddress(1, 0, 0, 0, 0, 0, 0));
        memspec->rank_stride = dramSys->getAddressDecoder().encodeAddress(
            DRAMSys::DecodedAddress(0, 1, 0, 0, 0, 0, 0));
        memspec->bankgroup_stride = dramSys->getAddressDecoder().encodeAddress(
            DRAMSys::DecodedAddress(0, 0, 0, 1, 0, 0, 0));
        memspec->bank_stride = dramSys->getAddressDecoder().encodeAddress(
            DRAMSys::DecodedAddress(0, 0, 0, 0, 1, 0, 0));
        memspec->row_stride = dramSys->getAddressDecoder().encodeAddress(
            DRAMSys::DecodedAddress(0, 0, 0, 0, 0, 1, 0));
        memspec->column_stride = dramSys->getAddressDecoder().encodeAddress(
            DRAMSys::DecodedAddress(0, 0, 0, 0, 0, 0, 1));
    }
    int res = id;
    id += 1;
    return res;
}

int dram_can_accept_req(int dram_id) {
    return list_of_conv[dram_id]->dram_can_accept_req();
}

int dram_has_read_rsp(int dram_id) {
    return list_of_conv[dram_id]->dram_has_read_rsp();
}

int dram_has_write_rsp(int dram_id) {
    return list_of_conv[dram_id]->dram_has_write_rsp();
}

int dram_get_write_rsp(int dram_id) { return list_of_conv[dram_id]->dram_get_write_rsp(); }

void dram_write_buffer(int dram_id, int byte_int, int idx) {
    ((uint8_t *)(list_of_wbuffer[dram_id]))[idx] = (uint8_t)byte_int;
}

void dram_write_strobe(int dram_id, int strob_int, int idx) {
    ((uint8_t *)(list_of_wstrobe[dram_id]))[idx] =
        strob_int != 0 ? TLM_BYTE_ENABLED : TLM_BYTE_DISABLED;
}

void dram_send_req(int dram_id, uint64_t addr, uint64_t length, uint64_t is_write,
                   uint64_t strob_enable) {

    if (is_write && strob_enable && (length > list_of_DRAMburst[dram_id])) {
        if (length % list_of_DRAMburst[dram_id] != 0)
            SC_REPORT_FATAL("dramsys_conv", "cannot tackle strob write with misaligned size");
        int num_subreq = length / list_of_DRAMburst[dram_id];
        uint64_t sub_addr = addr;
        uint8_t *wbuf_ptr = list_of_wbuffer[dram_id];
        uint8_t *wstrb_ptr = list_of_wstrobe[dram_id];
        for (int i = 0; i < num_subreq; ++i) {
            list_of_conv[dram_id]->dram_send_req(sub_addr, list_of_DRAMburst[dram_id], is_write,
                                                 strob_enable, wbuf_ptr, wstrb_ptr);
            sub_addr = sub_addr + list_of_DRAMburst[dram_id];
            wbuf_ptr = wbuf_ptr + list_of_DRAMburst[dram_id];
            wstrb_ptr = wstrb_ptr + list_of_DRAMburst[dram_id];
        }

    } else {
        list_of_conv[dram_id]->dram_send_req(addr, length, is_write, strob_enable,
                                             list_of_wbuffer[dram_id], list_of_wstrobe[dram_id]);
    }
}

void dram_get_read_rsp(int dram_id, uint64_t length, const svOpenArrayHandle buf) {

    list_of_conv[dram_id]->dram_get_read_rsp(length, (uint8_t *)buf);
}

int dram_get_read_rsp_byte(int dram_id) {
    uint8_t byte;
    int byte_int;
    byte = list_of_conv[dram_id]->dram_get_read_rsp_byte();
    byte_int = (int)byte;
    return byte_int;
}

int dram_get_inflight_read(int dram_id) {
     return list_of_conv[dram_id]->inflight_read_cnt;
}

void close_dram(int dram_id) {
    // Print statistics
    std::stringstream stats;
    DRAMSys::Stats::PrettyFormat::collectStats(list_of_DRAMsys[dram_id], stats);
    std::cout << stats.str() << std::endl;

    if (dram_id == 0)
        sc_stop();

    delete list_of_conv[dram_id];
    delete list_of_DRAMsys[dram_id];
}

void dram_register_async_callback(int dram_id, CallbackInstance_t instance,
                                  AsynCallbackResp_Meth *resp_meth,
                                  AsynCallbackUpdateReq_Meth *req_meth) {
    list_of_conv[dram_id]->registerCBInstance(instance);
    list_of_conv[dram_id]->registerCBRespMeth(resp_meth);
    list_of_conv[dram_id]->registerCBUpdateReqMeth(req_meth);
}

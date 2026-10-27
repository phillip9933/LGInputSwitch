#include "adl_command.h"
#include "../../core/ddc_packet.h"
#include "adl_session.h"

int SendAdlCommand(int adapter, int display, const lg::InputCommand &command)
{
    auto packet = lg::EncodeDdcWrite(command);
    if (adapter < 0 || display < 0 || !packet)
        return ADL_ERR_INVALID_PARAM;
    if (!InitializeAdl())
        return ADL_ERR;
    int received = 0;
    const int status = Adl().writeBlock(adapter, display, 0, 0, static_cast<int>(packet->size()),
                                        reinterpret_cast<char *>(packet->data()), &received, nullptr);
    // Preserve the existing settling interval without sending a readback request.
    Sleep(700);
    return status;
}

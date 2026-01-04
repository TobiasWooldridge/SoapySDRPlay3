/*
 * SDRplay Worker Process - Main Entry Point
 *
 * This executable runs as a subprocess to manage a single SDRplay device.
 * It receives commands via pipe and writes IQ samples to shared memory.
 */

#include "SoapySDRPlayWorker.hpp"
#include <SoapySDR/Logger.h>

int main(int argc, char* argv[])
{
    // Check if we're being run as a worker
    if (!SoapySDRPlayWorker::isWorkerMode(argc, argv))
    {
        SoapySDR_log(SOAPY_SDR_ERROR,
            "sdrplay_worker: Must be run with --sdrplay-worker flag\n"
            "This executable is meant to be spawned by SoapySDRPlay proxy mode.");
        return 1;
    }

    return SoapySDRPlayWorker::runAsWorker(argc, argv);
}

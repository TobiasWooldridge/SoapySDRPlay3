/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Charles J. Cliffe
 * Copyright (c) 2020 Franco Venturi - changes for SDRplay API version 3
 *                                     and Dual Tuner for RSPduo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "SoapySDRPlay.hpp"

#if defined(_M_X64) || defined(_M_IX86)
#define strcasecmp _stricmp
#elif defined (__GNUC__)
#include <strings.h>
#endif

/*******************************************************************
* Settings API
******************************************************************/

unsigned char SoapySDRPlay::stringToHWVer(std::string hwVer)
{
   if (strcasecmp(hwVer.c_str(), "RSP1") == 0)
   {
      return SDRPLAY_RSP1_ID;
   }
   else if (strcasecmp(hwVer.c_str(), "RSP1A") == 0)
   {
      return SDRPLAY_RSP1A_ID;
   }
   else if (strcasecmp(hwVer.c_str(), "RSP1B") == 0)
   {
      return SDRPLAY_RSP1B_ID;
   }
   else if (strcasecmp(hwVer.c_str(), "RSP2") == 0)
   {
      return SDRPLAY_RSP2_ID;
   }
   else if (strcasecmp(hwVer.c_str(), "RSPduo") == 0)
   {
      return SDRPLAY_RSPduo_ID;
   }
   else if (strcasecmp(hwVer.c_str(), "RSPdx") == 0)
   {
      return SDRPLAY_RSPdx_ID;
   }
   else if (strcasecmp(hwVer.c_str(), "RSPdx-R2") == 0)
   {
      return SDRPLAY_RSPdxR2_ID;
   }
   return 0;
}

std::string SoapySDRPlay::HWVertoString(unsigned char hwVer)
{
   switch (hwVer)
   {
   case SDRPLAY_RSP1_ID:
      return "RSP1";
      break;
   case SDRPLAY_RSP1A_ID:
      return "RSP1A";
      break;
   case SDRPLAY_RSP1B_ID:
      return "RSP1B";
      break;
   case SDRPLAY_RSP2_ID:
      return "RSP2";
      break;
   case SDRPLAY_RSPduo_ID:
      return "RSPduo";
      break;
   case SDRPLAY_RSPdx_ID:
      return "RSPdx";
      break;
   case SDRPLAY_RSPdxR2_ID:
      return "RSPdx-R2";
      break;
   }
   return "";
}

sdrplay_api_RspDuoModeT SoapySDRPlay::stringToRSPDuoMode(std::string rspDuoMode)
{
   if (strcasecmp(rspDuoMode.c_str(), "Single Tuner") == 0)
   {
      return sdrplay_api_RspDuoMode_Single_Tuner;
   }
   else if (strcasecmp(rspDuoMode.c_str(), "Dual Tuner") == 0)
   {
      return sdrplay_api_RspDuoMode_Dual_Tuner;
   }
   else if (strcasecmp(rspDuoMode.c_str(), "Master") == 0)
   {
      return sdrplay_api_RspDuoMode_Master;
   }
   else if (strcasecmp(rspDuoMode.c_str(), "Slave") == 0)
   {
      return sdrplay_api_RspDuoMode_Slave;
   }
   return sdrplay_api_RspDuoMode_Unknown;
}

std::string SoapySDRPlay::RSPDuoModetoString(sdrplay_api_RspDuoModeT rspDuoMode)
{
   switch (rspDuoMode)
   {
   case sdrplay_api_RspDuoMode_Unknown:
      return "";
      break;
   case sdrplay_api_RspDuoMode_Single_Tuner:
      return "Single Tuner";
      break;
   case sdrplay_api_RspDuoMode_Dual_Tuner:
      return "Dual Tuner";
      break;
   case sdrplay_api_RspDuoMode_Master:
      return "Master";
      break;
   case sdrplay_api_RspDuoMode_Slave:
      return "Slave";
      break;
   }
   return "";
}

SoapySDR::ArgInfoList SoapySDRPlay::getSettingInfo(void) const
{
    SoapySDR::ArgInfoList setArgs;

    // call selectDevice() because CubicSDR may think the device is
    // already selected - fv
    // here we need to cast away the constness of this, since selectDevice()
    // makes changes to its members
    SoapySDRPlay *non_const_this = const_cast<SoapySDRPlay*>(this);
    non_const_this->selectDevice();

#ifdef RF_GAIN_IN_MENU
    if (device.hwVer == SDRPLAY_RSP2_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       setArgs.push_back(RfGainArg);
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       RfGainArg.options.push_back("9");
       setArgs.push_back(RfGainArg);
    }
    else if (device.hwVer == SDRPLAY_RSP1A_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       RfGainArg.options.push_back("9");
       setArgs.push_back(RfGainArg);
    }
    else if (device.hwVer == SDRPLAY_RSP1B_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       RfGainArg.options.push_back("9");
       setArgs.push_back(RfGainArg);
    }
    else if (device.hwVer == SDRPLAY_RSPdx_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       RfGainArg.options.push_back("9");
       RfGainArg.options.push_back("10");
       RfGainArg.options.push_back("11");
       RfGainArg.options.push_back("12");
       RfGainArg.options.push_back("13");
       RfGainArg.options.push_back("14");
       RfGainArg.options.push_back("15");
       RfGainArg.options.push_back("16");
       RfGainArg.options.push_back("17");
       RfGainArg.options.push_back("18");
       RfGainArg.options.push_back("19");
       RfGainArg.options.push_back("20");
       RfGainArg.options.push_back("21");
       RfGainArg.options.push_back("22");
       RfGainArg.options.push_back("23");
       RfGainArg.options.push_back("24");
       RfGainArg.options.push_back("25");
       RfGainArg.options.push_back("26");
       RfGainArg.options.push_back("27");
       setArgs.push_back(RfGainArg);
    }
    else if (device.hwVer == SDRPLAY_RSPdxR2_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       RfGainArg.options.push_back("9");
       RfGainArg.options.push_back("10");
       RfGainArg.options.push_back("11");
       RfGainArg.options.push_back("12");
       RfGainArg.options.push_back("13");
       RfGainArg.options.push_back("14");
       RfGainArg.options.push_back("15");
       RfGainArg.options.push_back("16");
       RfGainArg.options.push_back("17");
       RfGainArg.options.push_back("18");
       RfGainArg.options.push_back("19");
       RfGainArg.options.push_back("20");
       RfGainArg.options.push_back("21");
       RfGainArg.options.push_back("22");
       RfGainArg.options.push_back("23");
       RfGainArg.options.push_back("24");
       RfGainArg.options.push_back("25");
       RfGainArg.options.push_back("26");
       RfGainArg.options.push_back("27");
       setArgs.push_back(RfGainArg);
    }
    else
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "1";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       setArgs.push_back(RfGainArg);
    }
#endif

    SoapySDR::ArgInfo IQcorrArg;
    IQcorrArg.key = "iqcorr_ctrl";
    IQcorrArg.value = "true";
    IQcorrArg.name = "IQ Correction";
    IQcorrArg.description = "IQ Correction Control";
    IQcorrArg.type = SoapySDR::ArgInfo::BOOL;
    setArgs.push_back(IQcorrArg);

    SoapySDR::ArgInfo SetPointArg;
    SetPointArg.key = "agc_setpoint";
    SetPointArg.value = "-30";
    SetPointArg.name = "AGC Setpoint";
    SetPointArg.description = "AGC Setpoint (dBfs)";
    SetPointArg.type = SoapySDR::ArgInfo::INT;
    SetPointArg.range = SoapySDR::Range(-60, 0);
    setArgs.push_back(SetPointArg);

    if (device.hwVer == SDRPLAY_RSP2_ID) // RSP2/RSP2pro
    {
       SoapySDR::ArgInfo ExtRefArg;
       ExtRefArg.key = "extref_ctrl";
       ExtRefArg.value = "true";
       ExtRefArg.name = "ExtRef Enable";
       ExtRefArg.description = "External Reference Control";
       ExtRefArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(ExtRefArg);

       SoapySDR::ArgInfo BiasTArg;
       BiasTArg.key = "biasT_ctrl";
       BiasTArg.value = "true";
       BiasTArg.name = "BiasT Enable";
       BiasTArg.description = "BiasT Control";
       BiasTArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(BiasTArg);

       SoapySDR::ArgInfo RfNotchArg;
       RfNotchArg.key = "rfnotch_ctrl";
       RfNotchArg.value = "true";
       RfNotchArg.name = "RfNotch Enable";
       RfNotchArg.description = "RF Notch Filter Control";
       RfNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(RfNotchArg);
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID) // RSPduo
    {
       SoapySDR::ArgInfo ExtRefArg;
       ExtRefArg.key = "extref_ctrl";
       ExtRefArg.value = "true";
       ExtRefArg.name = "ExtRef Enable";
       ExtRefArg.description = "External Reference Control";
       ExtRefArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(ExtRefArg);

       SoapySDR::ArgInfo BiasTArg;
       BiasTArg.key = "biasT_ctrl";
       BiasTArg.value = "true";
       BiasTArg.name = "BiasT Enable";
       BiasTArg.description = "BiasT Control";
       BiasTArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(BiasTArg);

       SoapySDR::ArgInfo RfNotchArg;
       RfNotchArg.key = "rfnotch_ctrl";
       RfNotchArg.value = "true";
       RfNotchArg.name = "RfNotch Enable";
       RfNotchArg.description = "RF Notch Filter Control";
       RfNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(RfNotchArg);

       SoapySDR::ArgInfo DabNotchArg;
       DabNotchArg.key = "dabnotch_ctrl";
       DabNotchArg.value = "true";
       DabNotchArg.name = "DabNotch Enable";
       DabNotchArg.description = "DAB Notch Filter Control";
       DabNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(DabNotchArg);
    }
    else if (device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID) // RSP1A and RSP1B
    {
       SoapySDR::ArgInfo BiasTArg;
       BiasTArg.key = "biasT_ctrl";
       BiasTArg.value = "true";
       BiasTArg.name = "BiasT Enable";
       BiasTArg.description = "BiasT Control";
       BiasTArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(BiasTArg);

       SoapySDR::ArgInfo RfNotchArg;
       RfNotchArg.key = "rfnotch_ctrl";
       RfNotchArg.value = "true";
       RfNotchArg.name = "RfNotch Enable";
       RfNotchArg.description = "RF Notch Filter Control";
       RfNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(RfNotchArg);

       SoapySDR::ArgInfo DabNotchArg;
       DabNotchArg.key = "dabnotch_ctrl";
       DabNotchArg.value = "true";
       DabNotchArg.name = "DabNotch Enable";
       DabNotchArg.description = "DAB Notch Filter Control";
       DabNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(DabNotchArg);
    }
    else if (device.hwVer == SDRPLAY_RSPdx_ID) // RSPdx
    {
       SoapySDR::ArgInfo BiasTArg;
       BiasTArg.key = "biasT_ctrl";
       BiasTArg.value = "true";
       BiasTArg.name = "BiasT Enable";
       BiasTArg.description = "BiasT Control";
       BiasTArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(BiasTArg);

       SoapySDR::ArgInfo RfNotchArg;
       RfNotchArg.key = "rfnotch_ctrl";
       RfNotchArg.value = "true";
       RfNotchArg.name = "RfNotch Enable";
       RfNotchArg.description = "RF Notch Filter Control";
       RfNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(RfNotchArg);

       SoapySDR::ArgInfo DabNotchArg;
       DabNotchArg.key = "dabnotch_ctrl";
       DabNotchArg.value = "true";
       DabNotchArg.name = "DabNotch Enable";
       DabNotchArg.description = "DAB Notch Filter Control";
       DabNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(DabNotchArg);

       SoapySDR::ArgInfo HDRArg;
       HDRArg.key = "hdr_ctrl";
       HDRArg.value = "true";
       HDRArg.name = "HDR Enable";
       HDRArg.description = "RSPdx HDR Control";
       HDRArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(HDRArg);
    }
    else if (device.hwVer == SDRPLAY_RSPdxR2_ID) // RSPdx-R2
    {
       SoapySDR::ArgInfo BiasTArg;
       BiasTArg.key = "biasT_ctrl";
       BiasTArg.value = "true";
       BiasTArg.name = "BiasT Enable";
       BiasTArg.description = "BiasT Control";
       BiasTArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(BiasTArg);

       SoapySDR::ArgInfo RfNotchArg;
       RfNotchArg.key = "rfnotch_ctrl";
       RfNotchArg.value = "true";
       RfNotchArg.name = "RfNotch Enable";
       RfNotchArg.description = "RF Notch Filter Control";
       RfNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(RfNotchArg);

       SoapySDR::ArgInfo DabNotchArg;
       DabNotchArg.key = "dabnotch_ctrl";
       DabNotchArg.value = "true";
       DabNotchArg.name = "DabNotch Enable";
       DabNotchArg.description = "DAB Notch Filter Control";
       DabNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(DabNotchArg);

       SoapySDR::ArgInfo HDRArg;
       HDRArg.key = "hdr_ctrl";
       HDRArg.value = "true";
       HDRArg.name = "HDR Enable";
       HDRArg.description = "RSPdx HDR Control";
       HDRArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(HDRArg);
    }

    return setArgs;
}

void SoapySDRPlay::writeSetting(const std::string &key, const std::string &value)
{
   std::lock_guard <std::mutex> lock(_general_state_mutex);

#ifdef RF_GAIN_IN_MENU
   if (key == "rfgain_sel")
   {
      chParams->tunerParams.gain.LNAstate = static_cast<unsigned char>(stoul(value));
      if (streamActive)
      {
         executeApiUpdate(sdrplay_api_Update_Tuner_Gr, sdrplay_api_Update_Ext1_None,
                          &gr_changed, "Tuner_Gr");
      }
   }
   else
#endif
   if (key == "iqcorr_ctrl")
   {
      if (value == "false") chParams->ctrlParams.dcOffset.IQenable = 0;
      else                  chParams->ctrlParams.dcOffset.IQenable = 1;
      chParams->ctrlParams.dcOffset.DCenable = 1;
      if (streamActive)
      {
         sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Ctrl_DCoffsetIQimbalance, sdrplay_api_Update_Ext1_None);
         if (err != sdrplay_api_Success)
         {
            SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Ctrl_DCoffsetIQimbalance) failed: %s", sdrplay_api_GetErrorString(err));
         }
      }
   }
   else if (key == "agc_setpoint")
   {
      chParams->ctrlParams.agc.setPoint_dBfs = stoi(value);
      if (streamActive)
      {
         sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Ctrl_Agc, sdrplay_api_Update_Ext1_None);
         if (err != sdrplay_api_Success)
         {
            SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Ctrl_Agc) failed: %s", sdrplay_api_GetErrorString(err));
         }
      }
   }
   else if (key == "extref_ctrl")
   {
      unsigned char extRef;
      if (value == "false") extRef = 0;
      else                  extRef = 1;
      if (device.hwVer == SDRPLAY_RSP2_ID)
      {
         deviceParams->devParams->rsp2Params.extRefOutputEn = extRef;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_ExtRefControl, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Rsp2_ExtRefControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      if (device.hwVer == SDRPLAY_RSPduo_ID)
      {
         // can't get extRefOutputEn for RSPduo slaves
         if (deviceParams->devParams)
         {
           deviceParams->devParams->rspDuoParams.extRefOutputEn = extRef;
           if (streamActive)
           {
             sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_ExtRefControl, sdrplay_api_Update_Ext1_None);
             if (err != sdrplay_api_Success)
             {
                SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDuo_ExtRefControl) failed: %s", sdrplay_api_GetErrorString(err));
             }
           }
         }
      }
   }
   else if (key == "biasT_ctrl")
   {
      unsigned char biasTen;
      if (value == "false") biasTen = 0;
      else                  biasTen = 1;
      if (device.hwVer == SDRPLAY_RSP2_ID)
      {
         chParams->rsp2TunerParams.biasTEnable = biasTen;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_BiasTControl, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Rsp2_BiasTControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPduo_ID)
      {
         chParams->rspDuoTunerParams.biasTEnable = biasTen;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_BiasTControl, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDuo_BiasTControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID)
      {
         chParams->rsp1aTunerParams.biasTEnable = biasTen;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp1a_BiasTControl, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Rsp1a_BiasTControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPdx_ID)
      {
         deviceParams->devParams->rspDxParams.biasTEnable = biasTen;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_BiasTControl);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDx_BiasTControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPdxR2_ID)
      {
         deviceParams->devParams->rspDxParams.biasTEnable = biasTen;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_BiasTControl);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDx_BiasTControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
   }
   else if (key == "rfnotch_ctrl")
   {
      unsigned char notchEn;
      if (value == "false") notchEn = 0;
      else                  notchEn = 1;
      if (device.hwVer == SDRPLAY_RSP2_ID)
      {
         chParams->rsp2TunerParams.rfNotchEnable = notchEn;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_RfNotchControl, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Rsp2_RfNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPduo_ID)
      {
        if (device.tuner == sdrplay_api_Tuner_A && chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_1)
        {
          chParams->rspDuoTunerParams.tuner1AmNotchEnable = notchEn;
          if (streamActive)
          {
             sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_Tuner1AmNotchControl, sdrplay_api_Update_Ext1_None);
             if (err != sdrplay_api_Success)
             {
                SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDuo_Tuner1AmNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
             }
          }
        }
        if (chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_2)
        {
          chParams->rspDuoTunerParams.rfNotchEnable = notchEn;
          if (streamActive)
          {
             sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_RfNotchControl, sdrplay_api_Update_Ext1_None);
             if (err != sdrplay_api_Success)
             {
                SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDuo_RfNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
             }
          }
        }
      }
      else if (device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID)
      {
         deviceParams->devParams->rsp1aParams.rfNotchEnable = notchEn;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp1a_RfNotchControl, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Rsp1a_RfNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPdx_ID)
      {
         deviceParams->devParams->rspDxParams.rfNotchEnable = notchEn;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_RfNotchControl);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDx_RfNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPdxR2_ID)
      {
         deviceParams->devParams->rspDxParams.rfNotchEnable = notchEn;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_RfNotchControl);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDx_RfNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
   }
   else if (key == "dabnotch_ctrl")
   {
      unsigned char dabNotchEn;
      if (value == "false") dabNotchEn = 0;
      else                  dabNotchEn = 1;
      if (device.hwVer == SDRPLAY_RSPduo_ID)
      {
         chParams->rspDuoTunerParams.rfDabNotchEnable = dabNotchEn;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_RfDabNotchControl, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDuo_RfDabNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      if (device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID)
      {
         deviceParams->devParams->rsp1aParams.rfDabNotchEnable = dabNotchEn;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp1a_RfDabNotchControl, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Rsp1a_RfDabNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPdx_ID)
      {
         deviceParams->devParams->rspDxParams.rfDabNotchEnable = dabNotchEn;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_RfDabNotchControl);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDx_RfDabNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPdxR2_ID)
      {
         deviceParams->devParams->rspDxParams.rfDabNotchEnable = dabNotchEn;
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_RfDabNotchControl);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDx_RfDabNotchControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
   }
   else if (key == "hdr_ctrl")
   {
      unsigned char hdrEn;
      if (value == "false") hdrEn = 0;
      else                  hdrEn = 1;
      if (device.hwVer == SDRPLAY_RSPdx_ID)
      {
         deviceParams->devParams->rspDxParams.hdrEnable = hdrEn;
         SoapySDR_logf(SOAPY_SDR_INFO, "--> rspDxParams.hdrEnable=%d", deviceParams->devParams->rspDxParams.hdrEnable);
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_HdrEnable);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDx_HdrEnable) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
      else if (device.hwVer == SDRPLAY_RSPdxR2_ID)
      {
         deviceParams->devParams->rspDxParams.hdrEnable = hdrEn;
         SoapySDR_logf(SOAPY_SDR_INFO, "--> rspDxParams.hdrEnable=%d", deviceParams->devParams->rspDxParams.hdrEnable);
         if (streamActive)
         {
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_HdrEnable);
            if (err != sdrplay_api_Success)
            {
               SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(RspDx_HdrEnable) failed: %s", sdrplay_api_GetErrorString(err));
            }
         }
      }
   }
}

std::string SoapySDRPlay::readSetting(const std::string &key) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

#ifdef RF_GAIN_IN_MENU
    if (key == "rfgain_sel")
    {
       return std::to_string(static_cast<unsigned int>(chParams->tunerParams.gain.LNAstate));
    }
    else
#endif
    if (key == "iqcorr_ctrl")
    {
       if (chParams->ctrlParams.dcOffset.IQenable == 0) return "false";
       else                                             return "true";
    }
    else if (key == "agc_setpoint")
    {
       return std::to_string(chParams->ctrlParams.agc.setPoint_dBfs);
    }
    else if (key == "extref_ctrl")
    {
       if (!deviceParams->devParams) {
          return "unknown";
       }
       unsigned char extRef = 0;
       if (device.hwVer == SDRPLAY_RSP2_ID) extRef = deviceParams->devParams->rsp2Params.extRefOutputEn;
       else if (device.hwVer == SDRPLAY_RSPduo_ID) extRef = deviceParams->devParams->rspDuoParams.extRefOutputEn;
       if (extRef == 0) return "false";
       else             return "true";
    }
    else if (key == "biasT_ctrl")
    {
       unsigned char biasTen = 0;
       if (device.hwVer == SDRPLAY_RSP2_ID) biasTen = chParams->rsp2TunerParams.biasTEnable;
       else if (device.hwVer == SDRPLAY_RSPduo_ID) biasTen = chParams->rspDuoTunerParams.biasTEnable;
       else if (device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID) biasTen = chParams->rsp1aTunerParams.biasTEnable;
       else if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
          if (!deviceParams->devParams) return "unknown";
          biasTen = deviceParams->devParams->rspDxParams.biasTEnable;
       }
       if (biasTen == 0) return "false";
       else              return "true";
    }
    else if (key == "rfnotch_ctrl")
    {
       unsigned char notchEn = 0;
       if (device.hwVer == SDRPLAY_RSP2_ID) notchEn = chParams->rsp2TunerParams.rfNotchEnable;
       else if (device.hwVer == SDRPLAY_RSPduo_ID)
       {
          if (device.tuner == sdrplay_api_Tuner_A && chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_1)
          {
             notchEn = chParams->rspDuoTunerParams.tuner1AmNotchEnable;
          }
          if (chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_2)
          {
             notchEn = chParams->rspDuoTunerParams.rfNotchEnable;
          }
       }
       else if (device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID) {
          if (!deviceParams->devParams) return "unknown";
          notchEn = deviceParams->devParams->rsp1aParams.rfNotchEnable;
       }
       else if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
          if (!deviceParams->devParams) return "unknown";
          notchEn = deviceParams->devParams->rspDxParams.rfNotchEnable;
       }
       if (notchEn == 0) return "false";
       else              return "true";
    }
    else if (key == "dabnotch_ctrl")
    {
       unsigned char dabNotchEn = 0;
       if (device.hwVer == SDRPLAY_RSPduo_ID) dabNotchEn = chParams->rspDuoTunerParams.rfDabNotchEnable;
       else if (device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID) {
          if (!deviceParams->devParams) return "unknown";
          dabNotchEn = deviceParams->devParams->rsp1aParams.rfDabNotchEnable;
       }
       else if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
          if (!deviceParams->devParams) return "unknown";
          dabNotchEn = deviceParams->devParams->rspDxParams.rfDabNotchEnable;
       }
       if (dabNotchEn == 0) return "false";
       else                 return "true";
    }
    else if (key == "hdr_ctrl")
    {
       unsigned char hdrEn = 0;
       if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
          if (!deviceParams->devParams) return "unknown";
          hdrEn = deviceParams->devParams->rspDxParams.hdrEnable;
       }
       if (hdrEn == 0) return "false";
       else            return "true";
    }

    // SoapySDR_logf(SOAPY_SDR_WARNING, "Unknown setting '%s'", key.c_str());
    return "";
}

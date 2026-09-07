/* Generated from AxDr_L_Motor/Parameter/parameter.yaml. DO NOT EDIT. */
#ifndef AXDR_PLOT_META_GENERATED_H
#define AXDR_PLOT_META_GENERATED_H

#include <cstdint>

struct AxDrPlotScaleEntry
{
    uint16_t id;
    float scale;
};

static const AxDrPlotScaleEntry kAxDrPlotScales[] =
{
    { 0x0001U, 0.001f }, // PARAM_ADC_IA
    { 0x0002U, 0.001f }, // PARAM_ADC_IB
    { 0x0003U, 0.001f }, // PARAM_ADC_IC
    { 0x0010U, 0.001f }, // PARAM_RUN_ID
    { 0x0011U, 0.001f }, // PARAM_RUN_IQ
    { 0x0012U, 0.001f }, // PARAM_RUN_UD
    { 0x0013U, 0.001f }, // PARAM_RUN_UQ
    { 0x0014U, 0.0002f }, // PARAM_RUN_THETA_E
    { 0x0015U, 0.001f }, // PARAM_RUN_UALPHA
    { 0x0016U, 0.001f }, // PARAM_RUN_UBETA
    { 0x0020U, 0.0002f }, // PARAM_OBS_THETA
    { 0x0021U, 0.1f }, // PARAM_OBS_WE
};

static const int kAxDrPlotScaleCount =
    static_cast<int>(sizeof(kAxDrPlotScales) / sizeof(kAxDrPlotScales[0]));

#endif // AXDR_PLOT_META_GENERATED_H

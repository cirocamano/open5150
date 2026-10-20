#pragma once
#include "Biquad.hpp"
namespace Amp5650 {
struct FilterSpec { FilterType type; float frequency, q, db; bool enabled; };
// Channel voicing filter banks and gain curves.
inline constexpr FilterSpec redImpedanceSpecs[] = {
    {FilterType::Lowshelf, 41.5130005f, 0.482557803f, 9.83411789f, true},
    {FilterType::Peaking, 121.332001f, 1.9093188f, 9.05072784f, true},
    {FilterType::Peaking, 143.645996f, 1.29697156f, -3.05008554f, true},
    {FilterType::Highshelf, 533.997009f, 0.762403965f, 2.17037678f, true},
    {FilterType::Highshelf, 11153.0957f, 0.517839253f, 7.88206339f, true},
};
inline constexpr FilterSpec redFixedSpecs[] = {
    {FilterType::Lowshelf, 150.130997f, 0.143374622f, -26.56464f, true},
    {FilterType::Peaking, 128.751007f, 0.272740006f, 4.8127141f, true},
    {FilterType::Highshelf, 20005.2344f, 0.768016934f, -5.55115604f, true},
};
inline constexpr FilterSpec redDriveSpecs[] = {
    {FilterType::Highshelf, 439.0f, 0.200306073f, -37.0408821f, true},
    {FilterType::Lowshelf, 117.320999f, 0.5f, -8.0f, true},
};
inline constexpr FilterSpec redBassBoostSpecs[] = {
    {FilterType::Peaking, 17.9050007f, 0.209928289f, 9.4231596f, true},
};
inline constexpr FilterSpec redBassCutSpecs[] = {
    {FilterType::Lowshelf, 194.460999f, 0.736744702f, -2.84781671f, true},
    {FilterType::Peaking, 583.117981f, 0.500733078f, -0.0770547912f, true},
    {FilterType::Peaking, 4838.89502f, 1.35203207f, 0.956763685f, false},
};
inline constexpr FilterSpec redMidBoostSpecs[] = {
    {FilterType::Lowshelf, 1107.73096f, 1.14167964f, 0.603595912f, false},
    {FilterType::Highshelf, 4088.42212f, 0.366289318f, 0.674229443f, true},
    {FilterType::Peaking, 290.487f, 0.665914476f, 2.17037678f, true},
};
inline constexpr FilterSpec redMidCutSpecs[] = {
    {FilterType::Highshelf, 12489.8945f, 0.720440388f, 2.74507713f, true},
    {FilterType::Highshelf, 763.728027f, 0.390612155f, -3.87842464f, true},
    {FilterType::Peaking, 738.150024f, 0.779777408f, -4.63613033f, true},
    {FilterType::Lowshelf, 27.6989994f, 0.356399834f, -2.35659242f, true},
};
inline constexpr FilterSpec redHighBoostSpecs[] = {
    {FilterType::Highshelf, 445.049011f, 0.452354729f, 3.67615581f, true},
    {FilterType::Highshelf, 17000.3125f, 0.582254648f, -7.20141268f, true},
};
inline constexpr FilterSpec redHighCutSpecs[] = {
    {FilterType::Highshelf, 1407.93701f, 0.712154567f, -4.93471766f, true},
    {FilterType::Lowshelf, 33.0530014f, 0.553120732f, -2.0002141f, true},
};
inline constexpr FilterSpec greenFixedSpecs[] = {
    {FilterType::Lowshelf, 20.7870007f, 0.318178236f, -36.3441772f, true},
    {FilterType::Highshelf, 15311.1797f, 0.514364541f, -7.33946896f, true},
    {FilterType::Peaking, 538.405029f, 0.631969452f, -2.27632713f, true},
};
inline constexpr FilterSpec greenDriveSpecs[] = {
    {FilterType::Highshelf, 439.0f, 0.200306073f, -37.0408821f, true},
    {FilterType::Lowshelf, 117.320999f, 0.5f, -8.0f, true},
};
inline constexpr FilterSpec greenBassBoostSpecs[] = {
    {FilterType::Peaking, 17.9050007f, 0.209928289f, 9.4231596f, true},
};
inline constexpr FilterSpec greenBassCutSpecs[] = {
    {FilterType::Lowshelf, 194.460999f, 0.736744702f, -2.84781671f, true},
    {FilterType::Peaking, 583.117981f, 0.500733078f, -0.0770547912f, true},
    {FilterType::Peaking, 4838.89502f, 1.35203207f, 0.956763685f, false},
};
inline constexpr FilterSpec greenMidBoostSpecs[] = {
    {FilterType::Lowshelf, 1107.73096f, 1.14167964f, 0.603595912f, false},
    {FilterType::Highshelf, 4088.42212f, 0.366289318f, 0.674229443f, true},
    {FilterType::Peaking, 290.487f, 0.665914476f, 2.17037678f, true},
};
inline constexpr FilterSpec greenMidCutSpecs[] = {
    {FilterType::Highshelf, 12489.8945f, 0.720440388f, 2.74507713f, true},
    {FilterType::Highshelf, 763.728027f, 0.390612155f, -3.87842464f, true},
    {FilterType::Peaking, 738.150024f, 0.779777408f, -4.63613033f, true},
    {FilterType::Lowshelf, 27.6989994f, 0.356399834f, -2.35659242f, true},
};
inline constexpr FilterSpec greenHighBoostSpecs[] = {
    {FilterType::Highshelf, 445.049011f, 0.452354729f, 3.67615581f, true},
    {FilterType::Highshelf, 17000.3125f, 0.582254648f, -7.20141268f, true},
};
inline constexpr FilterSpec greenHighCutSpecs[] = {
    {FilterType::Highshelf, 1291.21301f, 0.712154567f, -10.1680222f, true},
    {FilterType::Lowshelf, 33.0530014f, 0.553120732f, -2.0002141f, true},
    {FilterType::Peaking, 6587.37695f, 0.422953486f, -1.48972607f, true},
    {FilterType::Peaking, 81.4339981f, 0.201107934f, 0.231164381f, true},
};
inline constexpr FilterSpec greenBrightSpecs[] = {
    {FilterType::Highshelf, 2002.42505f, 0.472400993f, 7.5f, true},
};
inline constexpr float preRedCurve[] = {
    0.0013705442f, 0.0013705442f, 0.0013705442f, 0.00231306464f, 0.00416972907f, 0.00658589695f, 0.00956768263f, 0.013120126f,
    0.0172475148f, 0.0219535921f, 0.0272416621f, 0.0331146978f, 0.0395754017f, 0.0466263182f, 0.0542696193f, 0.06250754f,
    0.0713420138f, 0.0807749629f, 0.0908080712f, 0.101443015f, 0.112681434f, 0.124524742f, 0.136974469f, 0.150031865f,
    0.163698375f, 0.177975073f, 0.192863435f, 0.208364248f, 0.224478945f, 0.241208583f, 0.258553982f, 0.276516348f,
    0.295096546f, 0.314295709f, 0.334114671f, 0.354554206f, 0.375615299f, 0.397298753f, 0.419605345f, 0.442536026f,
    0.466091484f, 0.490272462f, 0.518213332f, 0.537473381f, 0.553561032f, 0.568023384f, 0.581414223f, 0.594016075f,
    0.605998695f, 0.617474794f, 0.628523886f, 0.639205396f, 0.649564862f, 0.659638464f, 0.669455409f, 0.679039836f,
    0.688412309f, 0.697589755f, 0.706587195f, 0.715417325f, 0.724091709f, 0.732620001f, 0.741011262f, 0.7492733f,
    0.757413268f, 0.765437663f, 0.773352265f, 0.7811625f, 0.788873076f, 0.796488583f, 0.804013073f, 0.811450481f,
    0.818804383f, 0.826077819f, 0.833274066f, 0.840395927f, 0.847446084f, 0.854427099f, 0.861341178f, 0.868190587f,
    0.874977469f, 0.881703854f, 0.888371468f, 0.8949821f, 0.901537538f, 0.908039212f, 0.914488792f, 0.920887589f,
    0.927236915f, 0.933538318f, 0.939792752f, 0.946001649f, 0.952165961f, 0.958286881f, 0.964365482f, 0.970402598f,
    0.976399422f, 0.982356608f, 0.98827529f, 0.994156122f, 1.0f,
};
inline constexpr float preGreenCurve[] = {
    0.0f, 0.0268070828f, 0.0462191068f, 0.0635634288f, 0.0796881244f, 0.094962649f, 0.109592132f, 0.12370611f,
    0.137393311f, 0.150717989f, 0.163728684f, 0.176463246f, 0.188951924f, 0.201219514f, 0.2132864f, 0.225170016f,
    0.236885011f, 0.248444229f, 0.259858638f, 0.271137953f, 0.282290846f, 0.293324888f, 0.304246992f, 0.315063208f,
    0.325779229f, 0.336399972f, 0.346930206f, 0.357373923f, 0.367735177f, 0.378017545f, 0.388224155f, 0.398358196f,
    0.40842244f, 0.4184196f, 0.428352118f, 0.438222229f, 0.448032141f, 0.457783848f, 0.467479259f, 0.477120221f,
    0.486708373f, 0.496245325f, 0.505732596f, 0.515171647f, 0.524563789f, 0.533910334f, 0.543212473f, 0.552471459f,
    0.561688304f, 0.570864201f, 0.579999983f, 0.589096785f, 0.598155499f, 0.60717696f, 0.616162002f, 0.625111461f,
    0.634026229f, 0.642906964f, 0.651754379f, 0.660569131f, 0.669352055f, 0.678103566f, 0.686824501f, 0.695515335f,
    0.704176664f, 0.712809086f, 0.721413136f, 0.72998935f, 0.738538086f, 0.747060061f, 0.755555511f, 0.764025092f,
    0.772469163f, 0.78088814f, 0.789282441f, 0.797652543f, 0.805998743f, 0.814321518f, 0.822621107f, 0.830897927f,
    0.839152396f, 0.84738481f, 0.85559541f, 0.863784671f, 0.871952832f, 0.880100131f, 0.888226986f, 0.896333635f,
    0.904420316f, 0.912487388f, 0.920535028f, 0.928563595f, 0.936573207f, 0.944564342f, 0.952537f, 0.960491538f,
    0.968428195f, 0.976347089f, 0.984248579f, 0.992132843f, 1.0f,
};
// shortcut: Pre curves use 0.1-step interpolation; increase resolution if finer control is needed.
inline float preCurve(float knob, bool red) {
    const auto* points = red ? preRedCurve : preGreenCurve;
    const float index = std::clamp(knob * 10.f, 0.f, 100.f);
    const int i = std::min(int(index), 99);
    return points[i] + (points[i + 1] - points[i]) * (index - i);
}
}

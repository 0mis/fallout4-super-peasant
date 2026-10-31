// OWNER: SKELETON MAPPING (R6 STEP 12, CORE lane). See core/pose_map.h.
#include "core/pose_map.h"

#include "config.h"
#include "core/body_model.gen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sp::posemap
{
	using namespace sp::math;
	using sp::body::kPartCount;

	namespace
	{
		// GENERATED (scratch gen_rest.py) from fo4/model/fo4_skeleton_dump_game.json: vanilla skeleton.nif, 129 nodes, game 1.11.240.
		// R = rows of the column-vector rotation (p_parent = R*(s*p_local)+t), i.e. sp::math::Mat3 m[r][c].
		constexpr RestBone kRestTable[] = {
			{ "skeleton.nif", "", { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "Root", "skeleton.nif", { 1.0, 0.0, 0.0, -0.0, 1.0, -0.0, -0.0, -0.0, 1.0 }, { -0.0, -0.0, 0.0 }, 1.0 },
			{ "COM", "Root", { 0.0, -1e-06, -1.0, 0.0, 1.0, -1e-06, 1.0, -0.0, 0.0 }, { 1.1e-05, 4.1e-05, 68.9113 }, 1.0 },
			{ "Pelvis", "COM", { 1.0, 0.0, 0.0, -0.0, 1.0, 0.0, -0.0, -0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "LLeg_Thigh", "Pelvis", { -0.99112, -0.000169, -0.132974, -0.038602, 0.957303, 0.286497, 0.127248, 0.289086, -0.948808 }, { 8e-06, 0.000431, 6.61507 }, 1.0 },
			{ "LLeg_Calf", "LLeg_Thigh", { 0.992104, 0.122658, -0.026177, -0.122658, 0.992448, 0.001587, 0.026174, 0.001636, 0.999656 }, { 31.5952, 2.1e-05, -3.1e-05 }, 1.0 },
			{ "LLeg_Foot", "LLeg_Calf", { 0.453297, -0.885551, -0.101593, 0.887978, 0.458554, -0.03499, 0.077572, -0.074351, 0.99421 }, { 31.94289, -1.8e-05, -1.8e-05 }, 1.0 },
			{ "LLeg_Toe1", "LLeg_Foot", { 0.825848, -0.563884, -0.003295, 0.563887, 0.825852, 7e-06, 0.002717, -0.001864, 0.999995 }, { 10.66192, 0.003762, -0.000739 }, 1.0 },
			{ "LLeg_Calf_skin", "LLeg_Calf", { 1.0, 1e-06, 6e-06, -1e-06, 1.0, -2e-06, -6e-06, 2e-06, 1.0 }, { -0.027271, -0.105606, 0.332731 }, 1.0 },
			{ "LLeg_Calf_Low_skin", "LLeg_Calf", { 1.0, 1e-06, 6e-06, -1e-06, 1.0, -2e-06, -6e-06, 2e-06, 1.0 }, { 15.15149, -1.360284, -0.667333 }, 1.0 },
			{ "LLeg_Thigh_skin", "LLeg_Thigh", { 1.0, -0.0, -6e-06, 0.0, 1.0, 1e-06, 6e-06, -1e-06, 1.0 }, { -0.04055, -0.101393, 0.332478 }, 1.0 },
			{ "LLeg_Thigh_Low_skin", "LLeg_Thigh", { 1.0, -0.0, -6e-06, 0.0, 1.0, 1e-06, 6e-06, -1e-06, 1.0 }, { 14.37264, -0.101386, -0.167401 }, 1.0 },
			{ "LLeg_Thigh_Fat_skin", "LLeg_Thigh", { 1.0, -0.0, -6e-06, 0.0, 1.0, 1e-06, 6e-06, -1e-06, 1.0 }, { 25.70045, -0.101381, -0.167344 }, 1.0 },
			{ "RLeg_Thigh", "Pelvis", { -0.99307, 0.005202, 0.117412, -0.029031, 0.957206, -0.287949, -0.113886, -0.289362, -0.950421 }, { 8e-06, 0.000431, -6.615068 }, 1.0 },
			{ "RLeg_Calf", "RLeg_Thigh", { 0.991077, 0.133293, 0.000112, -0.133294, 0.991076, 0.001395, 7.5e-05, -0.001397, 0.999999 }, { 31.59514, 2e-05, 2.9e-05 }, 1.0 },
			{ "RLeg_Foot", "RLeg_Calf", { 0.44741, -0.887312, 0.111813, 0.890614, 0.453439, 0.034629, -0.081427, 0.084089, 0.993126 }, { 31.94257, 1e-06, -2e-06 }, 1.0 },
			{ "RLeg_Toe1", "RLeg_Foot", { 0.83836, -0.544961, -0.013052, 0.544895, 0.838461, -0.008482, 0.015566, -1e-06, 0.999879 }, { 10.66154, -2e-06, 1.1e-05 }, 1.0 },
			{ "RLeg_Calf_skin", "RLeg_Calf", { 1.0, -0.0, -1e-06, 0.0, 1.0, 0.0, 1e-06, -0.0, 1.0 }, { -0.027306, -0.10516, -0.332705 }, 1.0 },
			{ "RLeg_Calf_Low_skin", "RLeg_Calf", { 1.0, 0.0, -0.0, -0.0, 1.0, 0.0, 0.0, -0.0, 1.0 }, { 15.15153, -1.3598, 0.667263 }, 1.0 },
			{ "RLeg_Thigh_skin", "RLeg_Thigh", { 1.0, -0.0, 0.0, 0.0, 1.0, 0.0, -0.0, -0.0, 1.0 }, { -0.040451, -0.101389, -0.332525 }, 1.0 },
			{ "RLeg_Thigh_Low_skin", "RLeg_Thigh", { 1.0, -0.0, -0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 14.37287, -0.101384, 0.167451 }, 1.0 },
			{ "RLeg_Thigh_Fat_skin", "RLeg_Thigh", { 1.0, -0.0, -0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 25.70069, -0.101385, 0.167447 }, 1.0 },
			{ "Pelvis_skin", "Pelvis", { 1.0, -0.0, 0.0, 0.0, 1.0, 0.0, -0.0, -0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "RButtFat_skin", "Pelvis", { -0.985233, 0.015868, 0.170479, -0.039406, 0.947949, -0.315974, -0.16662, -0.318026, -0.933326 }, { 2.115402, 0.035822, -6.405413 }, 1.0 },
			{ "LButtFat_skin", "Pelvis", { -0.982515, 0.007748, -0.186024, -0.049272, 0.952692, 0.299916, 0.179547, 0.303837, -0.935652 }, { 2.110802, 0.047385, 6.380761 }, 1.0 },
			{ "Pelvis_Rear_skin", "Pelvis", { 1.0, -0.0, 0.0, 0.0, 1.0, 0.0, -0.0, -0.0, 1.0 }, { 0.0, -2.039941, -0.0 }, 1.0 },
			{ "SPINE1", "COM", { 0.992464, -0.122538, -1e-06, 0.122538, 0.992464, 2e-06, 1e-06, -2e-06, 1.0 }, { 3.791992, -0.002948, -8e-06 }, 1.0 },
			{ "SPINE2", "SPINE1", { 0.984632, 0.174639, -0.0, -0.174639, 0.984632, -2e-06, -0.0, 2e-06, 1.0 }, { 8.704666, 0.0, 0.0 }, 1.0 },
			{ "Chest", "SPINE2", { 0.999831, -0.018372, 0.0, 0.018372, 0.999831, 0.0, -0.0, -0.0, 1.0 }, { 9.956283, -0.0, 0.0 }, 1.0 },
			{ "LArm_Collarbone", "Chest", { -0.404895, -0.005992, -0.914343, -0.264079, 0.958132, 0.110662, 0.875398, 0.286265, -0.389525 }, { 19.15323, -0.510421, 1.695077 }, 1.0 },
			{ "LArm_UpperArm", "LArm_Collarbone", { 0.916167, -0.252794, -0.311019, 0.253283, 0.966584, -0.039539, 0.310621, -0.042551, 0.949581 }, { 12.53657, 3e-06, 0.0 }, 1.0 },
			{ "LArm_ForeArm1", "LArm_UpperArm", { 0.855115, -0.514615, -0.062845, 0.515476, 0.856899, -0.002895, 0.055341, -0.02992, 0.998019 }, { 17.96831, 1.9e-05, 7e-06 }, 1.0 },
			{ "LArm_ForeArm2", "LArm_ForeArm1", { 1.0, -0.0, 0.0, 0.0, 0.999986, -0.00536, -0.0, 0.00536, 0.999986 }, { 6.151599, -2.2e-05, -4.5e-05 }, 1.0 },
			{ "LArm_ForeArm3", "LArm_ForeArm2", { 1.0, 0.0, 0.0, -0.0, 0.999986, -0.00536, -0.0, 0.00536, 0.999986 }, { 6.151586, -6.8e-05, 1.6e-05 }, 1.0 },
			{ "LArm_Hand", "LArm_ForeArm3", { 0.98845, 0.14557, -0.042145, 0.041364, 0.008387, 0.999109, 0.145793, -0.989312, 0.002269 }, { 6.151584, 3.3e-05, -6.5e-05 }, 1.0 },
			{ "LArm_Finger11", "LArm_Hand", { 0.583614, -0.698265, -0.414513, -0.301079, 0.288008, -0.909067, 0.754153, 0.655345, -0.042147 }, { 1.582954, -1.262685, 1.853245 }, 1.0 },
			{ "LArm_Finger12", "LArm_Finger11", { 0.714858, 0.694074, -0.085088, -0.69742, 0.716514, -0.0146, 0.050834, 0.069779, 0.996266 }, { 3.56955, 2.4e-05, -6.4e-05 }, 1.0 },
			{ "LArm_Finger13", "LArm_Finger12", { 0.867142, 0.49566, 0.048851, -0.496252, 0.868179, -7e-06, -0.042415, -0.024237, 0.998806 }, { 2.40176, -7e-06, 2e-05 }, 1.0 },
			{ "LArm_Finger21", "LArm_Hand", { 0.829148, 0.551688, -0.090298, -0.545741, 0.833819, 0.083142, 0.12116, -0.019658, 0.992438 }, { 7.501294, 0.430309, 2.277701 }, 1.0 },
			{ "LArm_Finger22", "LArm_Finger21", { 0.736613, 0.676314, -4e-06, -0.676314, 0.736613, -3e-06, 1e-06, 5e-06, 1.0 }, { 3.018161, 4.1e-05, -2.7e-05 }, 1.0 },
			{ "LArm_Finger23", "LArm_Finger22", { 0.882064, 0.47113, 1e-06, -0.47113, 0.882064, 5e-06, 1e-06, -5e-06, 1.0 }, { 1.850211, 6e-06, 1e-06 }, 1.0 },
			{ "LArm_Finger31", "LArm_Hand", { 0.773141, 0.63092, 0.064748, -0.632929, 0.760974, 0.142546, 0.040664, -0.151189, 0.987668 }, { 7.595699, 0.621028, 0.457422 }, 1.0 },
			{ "LArm_Finger32", "LArm_Finger31", { 0.692159, 0.721743, 0.002059, -0.721739, 0.692162, -0.002142, -0.002972, -3e-06, 0.999996 }, { 3.091624, 4.3e-05, -3.9e-05 }, 1.0 },
			{ "LArm_Finger33", "LArm_Finger32", { 0.826404, 0.563069, -0.003047, -0.563072, 0.826408, -0.000254, 0.002375, 0.001926, 0.999995 }, { 2.187935, 9e-06, -2e-06 }, 1.0 },
			{ "LArm_Finger41", "LArm_Hand", { 0.743988, 0.647145, 0.166388, -0.668193, 0.720302, 0.186236, 0.000672, -0.249737, 0.968314 }, { 7.463944, 0.350243, -1.438792 }, 1.0 },
			{ "LArm_Finger42", "LArm_Finger41", { 0.61478, 0.788699, 3e-06, -0.788699, 0.61478, -5e-06, -6e-06, 1e-06, 1.0 }, { 2.66439, 3.2e-05, -3e-05 }, 1.0 },
			{ "LArm_Finger43", "LArm_Finger42", { 0.836521, 0.547935, -2e-06, -0.547935, 0.836521, 1e-06, 2e-06, 0.0, 1.0 }, { 1.899728, 1.5e-05, 3e-06 }, 1.0 },
			{ "LArm_Finger51", "LArm_Hand", { 0.69553, 0.690911, 0.197181, -0.718298, 0.662177, 0.213468, 0.016919, -0.290108, 0.956844 }, { 6.637164, -0.357309, -3.018454 }, 1.0 },
			{ "LArm_Finger52", "LArm_Finger51", { 0.739293, 0.673383, 0.000706, -0.673383, 0.739294, -0.000329, -0.000743, -0.000232, 1.0 }, { 2.238247, 3.2e-05, -2.6e-05 }, 1.0 },
			{ "LArm_Finger53", "LArm_Finger52", { 0.643412, 0.76552, -0.000493, -0.76552, 0.643412, -1e-06, 0.000316, 0.000378, 1.0 }, { 1.665891, 3e-06, 0.0 }, 1.0 },
			{ "WeaponLeft", "LArm_Hand", { -0.993587, 0.027679, 0.109634, 0.109376, -0.010677, 0.993943, 0.028682, 0.99956, 0.007581 }, { 6.743556, -3.484691, -0.176368 }, 1.0 },
			{ "AnimObjectL1", "LArm_Hand", { 1.0, -0.0, -0.0, 0.0, 1.0, 0.0, 0.0, -0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "AnimObjectL3", "LArm_Hand", { 1.0, -0.0, -0.0, 0.0, 1.0, 0.0, 0.0, -0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "AnimObjectL2", "LArm_Hand", { 1.0, -0.0, -0.0, 0.0, 1.0, 0.0, 0.0, -0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "PipboyBone", "LArm_ForeArm3", { -0.996137, 0.086742, -0.013684, -0.085662, -0.994139, -0.06595, -0.019324, -0.064523, 0.997729 }, { 4.517029, -0.160801, 0.071516 }, 1.0 },
			{ "LArm_ForeArm3_skin", "LArm_ForeArm3", { 1.0, -0.0, 0.0, 0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { -0.215753, 0.127502, 0.244356 }, 1.0 },
			{ "LArm_ForeArm2_skin", "LArm_ForeArm2", { 1.0, 0.0, -0.0, -0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { -0.215563, 0.127336, 0.244612 }, 1.0 },
			{ "LArm_ForeArm1_skin", "LArm_ForeArm1", { 1.0, 0.0, -0.0, -0.0, 1.0, 0.0, 0.0, -0.0, 1.0 }, { -0.215569, 0.126519, 0.245004 }, 1.0 },
			{ "LArm_UpperTwist1", "LArm_UpperArm", { 1.0, -1e-06, 0.0, 1e-06, 0.999593, 0.028528, -0.0, -0.028528, 0.999593 }, { -0.759407, -6e-06, -1.6e-05 }, 1.0 },
			{ "LArm_UpperTwist2", "LArm_UpperTwist1", { 1.0, 0.0, 0.0, -0.0, 0.999898, -0.014266, -0.0, 0.014266, 0.999898 }, { 7.170016, 2e-06, -1.1e-05 }, 1.0 },
			{ "LArm_UpperTwist2_skin", "LArm_UpperTwist2", { 1.0, -0.0, 0.0, 0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { -0.249572, -0.027648, 0.243744 }, 1.0 },
			{ "LArm_UpperFat_skin", "LArm_UpperTwist2", { 1.0, -0.0, 0.0, 0.0, 1.0, 0.0, -0.0, -0.0, 1.0 }, { -1.31118, -4.9e-05, 0.000152 }, 1.0 },
			{ "LArm_UpperTwist1_skin", "LArm_UpperTwist1", { 1.0, -0.0, -0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 0.330643, -0.196122, 0.89164 }, 1.0 },
			{ "LArm_UpperArm_skin", "LArm_UpperArm", { 1.0, -0.0, -0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { -0.249691, -0.027596, 0.243652 }, 1.0 },
			{ "LArm_Collarbone_skin", "LArm_Collarbone", { 0.999898, -0.013855, -0.003543, 0.013873, 0.99989, 0.005156, 0.003471, -0.005205, 0.99998 }, { 4.2e-05, 1.2e-05, 0.000267 }, 1.0 },
			{ "LArm_ShoulderFat_skin", "LArm_Collarbone", { 0.97559, -0.140684, 0.16862, 0.137734, 0.990041, 0.029124, -0.171038, -0.005188, 0.985251 }, { 7.760723, 0.107693, 0.025841 }, 1.0 },
			{ "Neck", "Chest", { 0.912684, -0.408666, -2.7e-05, 0.408666, 0.912684, -1e-06, 2.5e-05, -1e-05, 1.0 }, { 22.08397, -3.767101, -9e-06 }, 1.0 },
			{ "HEAD", "Neck", { 0.948716, 0.316129, 2.5e-05, -0.316129, 0.948716, -1e-05, -2.7e-05, 1e-06, 1.0 }, { 8.224354, 0.0, -5e-06 }, 1.0 },
			{ "Head_skin", "HEAD", { 1.0, -0.0, 0.0, 0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { 0.000328, -2.2e-05, 0.0 }, 1.0 },
			{ "Face_skin", "HEAD", { -0.0, -1.0, 0.0, 1.0, -0.0, 0.0, -0.0, 0.0, 1.0 }, { -0.870429, 1.154643, -2e-06 }, 1.0 },
			{ "Neck_skin", "Neck", { 1.0, -0.0, 0.0, 0.0, 1.0, 0.0, -0.0, -0.0, 1.0 }, { 0.000427, -0.000187, -0.0 }, 1.0 },
			{ "Neck1_skin", "Neck", { 0.965926, 0.258819, -0.0, -0.258819, 0.965926, 0.0, 0.0, 0.0, 1.0 }, { 4.539709, 0.000147, -6e-06 }, 1.0 },
			{ "RArm_Collarbone", "Chest", { -0.40497, -0.006015, 0.91431, -0.26413, 0.958115, -0.110686, -0.875348, -0.286321, -0.389597 }, { 19.15322, -0.510418, -1.695124 }, 1.0 },
			{ "RArm_UpperArm", "RArm_Collarbone", { 0.9162, -0.25314, 0.310642, 0.253652, 0.96649, 0.03947, -0.310224, 0.042632, 0.949707 }, { 12.53426, 7e-06, -1.3e-05 }, 1.0 },
			{ "RArm_ForeArm1", "RArm_UpperArm", { 0.855315, -0.514189, 0.063602, 0.515073, 0.857141, 0.002875, -0.055994, 0.0303, 0.997971 }, { 17.9705, 0.000103, -5.6e-05 }, 1.0 },
			{ "RArm_ForeArm2", "RArm_ForeArm1", { 1.0, -0.0, 0.0, 0.0, 0.999986, 0.00536, -0.0, -0.00536, 0.999986 }, { 6.152768, 4e-05, -3.9e-05 }, 1.0 },
			{ "RArm_ForeArm3", "RArm_ForeArm2", { 1.0, 0.0, 0.0, -0.0, 0.999986, 0.00536, -0.0, -0.00536, 0.999986 }, { 6.152856, -9e-06, -9.1e-05 }, 1.0 },
			{ "RArm_Hand", "RArm_ForeArm3", { 0.98845, 0.145569, 0.042145, 0.041364, 0.008387, -0.999109, -0.145793, 0.989313, 0.002269 }, { 6.152939, 4.4e-05, -2.9e-05 }, 1.0 },
			{ "RArm_Finger11", "RArm_Hand", { 0.583614, -0.698265, 0.414513, -0.301079, 0.288008, 0.909067, -0.754153, -0.655345, -0.042148 }, { 1.583078, -1.262631, -1.853176 }, 1.0 },
			{ "RArm_Finger12", "RArm_Finger11", { 0.71486, 0.694071, 0.085089, -0.697417, 0.716517, 0.0146, -0.050834, -0.06978, 0.996266 }, { 3.569634, 0.000164, 4.1e-05 }, 1.0 },
			{ "RArm_Finger13", "RArm_Finger12", { 0.867142, 0.495659, -0.048851, -0.496251, 0.868179, 7e-06, 0.042415, 0.024237, 0.998806 }, { 2.401813, 2.7e-05, -2.1e-05 }, 1.0 },
			{ "RArm_Finger21", "RArm_Hand", { 0.829148, 0.551688, 0.090298, -0.545741, 0.833819, -0.083142, -0.12116, 0.019658, 0.992438 }, { 7.501817, 0.43035, -2.277641 }, 1.0 },
			{ "RArm_Finger22", "RArm_Finger21", { 0.736613, 0.676314, 4e-06, -0.676314, 0.736613, 3e-06, -1e-06, -5e-06, 1.0 }, { 3.018468, -6.9e-05, -1.1e-05 }, 1.0 },
			{ "RArm_Finger23", "RArm_Finger22", { 0.882064, 0.47113, -2e-06, -0.47113, 0.882064, -5e-06, -1e-06, 5e-06, 1.0 }, { 1.85036, -5.5e-05, 1e-06 }, 1.0 },
			{ "RArm_Finger31", "RArm_Hand", { 0.773141, 0.630921, -0.064748, -0.63293, 0.760973, -0.142546, -0.040664, 0.151189, 0.987668 }, { 7.596231, 0.621086, -0.457469 }, 1.0 },
			{ "RArm_Finger32", "RArm_Finger31", { 0.692159, 0.721742, -0.002059, -0.721739, 0.692162, 0.002142, 0.002971, 3e-06, 0.999996 }, { 3.091899, -7.3e-05, 1.2e-05 }, 1.0 },
			{ "RArm_Finger33", "RArm_Finger32", { 0.826403, 0.56307, 0.003047, -0.563074, 0.826407, 0.000254, -0.002375, -0.001926, 0.999995 }, { 2.188087, -6.3e-05, -5e-06 }, 1.0 },
			{ "RArm_Finger41", "RArm_Hand", { 0.743988, 0.647145, -0.166388, -0.668192, 0.720302, -0.186236, -0.000672, 0.249737, 0.968314 }, { 7.464419, 0.350385, 1.438638 }, 1.0 },
			{ "RArm_Finger42", "RArm_Finger41", { 0.61478, 0.788699, -3e-06, -0.788699, 0.61478, 5e-06, 6e-06, -1e-06, 1.0 }, { 2.664626, -7e-05, 2.6e-05 }, 1.0 },
			{ "RArm_Finger43", "RArm_Finger42", { 0.836521, 0.547935, 2e-06, -0.547935, 0.836521, -1e-06, -2e-06, -0.0, 1.0 }, { 1.899813, -6.6e-05, -1.3e-05 }, 1.0 },
			{ "RArm_Finger51", "RArm_Hand", { 0.695529, 0.690912, -0.197181, -0.718299, 0.662177, -0.213468, -0.016919, 0.290108, 0.956844 }, { 6.637601, -0.357199, 3.018234 }, 1.0 },
			{ "RArm_Finger52", "RArm_Finger51", { 0.739293, 0.673383, -0.000706, -0.673383, 0.739294, 0.000329, 0.000744, 0.000232, 1.0 }, { 2.238413, -5.9e-05, 2.4e-05 }, 1.0 },
			{ "RArm_Finger53", "RArm_Finger52", { 0.643412, 0.76552, 0.000493, -0.76552, 0.643412, 1e-06, -0.000316, -0.000378, 1.0 }, { 1.665994, -2.4e-05, -1e-05 }, 1.0 },
			{ "WEAPON", "RArm_Hand", { -0.993587, 0.02768, -0.109634, 0.109376, -0.010677, -0.993943, -0.028683, -0.99956, 0.007581 }, { 6.743987, -3.484592, 0.17628 }, 1.0 },
			{ "AnimObjectR1", "RArm_Hand", { 1.0, -0.0, -0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "AnimObjectR2", "RArm_Hand", { 1.0, -0.0, -0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "AnimObjectR3", "RArm_Hand", { 1.0, -0.0, -0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "RArm_ForeArm3_skin", "RArm_ForeArm3", { 1.0, 0.0, 0.0, -0.0, 1.0, 0.0, -0.0, -0.0, 1.0 }, { -0.215171, 0.128151, -0.244218 }, 1.0 },
			{ "RArm_ForeArm2_skin", "RArm_ForeArm2", { 1.0, -0.0, -0.0, 0.0, 1.0, 0.0, 0.0, -0.0, 1.0 }, { -0.215646, 0.127251, -0.244562 }, 1.0 },
			{ "RArm_ForeArm1_skin", "RArm_ForeArm1", { 1.0, -0.0, -0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { -0.215581, 0.1265, -0.245005 }, 1.0 },
			{ "RArm_UpperTwist1", "RArm_UpperArm", { 1.0, -0.0, -0.0, 0.0, 0.99959, -0.028618, 0.0, 0.028618, 0.99959 }, { -0.757203, 3.3e-05, -5e-05 }, 1.0 },
			{ "RArm_UpperTwist2", "RArm_UpperTwist1", { 1.0, -0.0, 0.0, 0.0, 0.999898, 0.014311, -0.0, -0.014311, 0.999898 }, { 7.169981, 3.4e-05, 3.9e-05 }, 1.0 },
			{ "RArm_UpperTwist2_skin", "RArm_UpperTwist2", { 1.0, -0.0, 0.0, 0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { -0.24945, -0.027691, -0.243851 }, 1.0 },
			{ "RArm_UpperFat_skin", "RArm_UpperTwist2", { 1.0, -0.0, -1e-06, 0.0, 1.0, 0.0, 1e-06, -0.0, 1.0 }, { -1.311197, -5.7e-05, -0.000114 }, 1.0 },
			{ "RArm_UpperTwist1_skin", "RArm_UpperTwist1", { 1.0, -0.0, -0.0, 0.0, 1.0, -1e-06, 0.0, 1e-06, 1.0 }, { 0.330299, -0.192694, -0.892924 }, 1.0 },
			{ "RArm_UpperArm_skin", "RArm_UpperArm", { 1.0, 0.0, 0.0, -0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { -0.248945, -0.027044, -0.244371 }, 1.0 },
			{ "RArm_Collarbone_skin", "RArm_Collarbone", { 0.999898, -0.013858, 0.003545, 0.013876, 0.99989, -0.005157, -0.003473, 0.005206, 0.99998 }, { 0.000114, -6e-06, -0.000252 }, 1.0 },
			{ "RArm_ShoulderFat_skin", "RArm_Collarbone", { 0.97548, -0.14069, -0.169251, 0.137701, 0.99004, -0.029327, 0.171692, 0.005302, 0.985137 }, { 7.760798, 0.1077, -0.025875 }, 1.0 },
			{ "L_RibHelper", "Chest", { -0.02598, -0.037785, 0.998948, 0.025008, 0.998948, 0.038435, -0.99935, 0.025981, -0.025008 }, { 8.36875, -0.648966, 5.030599 }, 1.0 },
			{ "R_RibHelper", "Chest", { 0.024203, -0.030815, 0.999232, 0.026732, 0.999187, 0.030166, -0.99935, 0.025981, 0.025007 }, { 8.368751, -0.648964, -5.030643 }, 1.0 },
			{ "Chest_skin", "Chest", { 0.999915, 0.013012, 0.0, -0.013012, 0.999915, -0.0, -0.0, 0.0, 1.0 }, { 0.004265, 0.31979, -1.4e-05 }, 1.0 },
			{ "LBreast_skin", "Chest", { 0.106523, 0.334438, -0.936378, -0.28403, 0.912731, 0.293681, 0.95288, 0.234676, 0.192217 }, { 15.01359, -0.774427, 3.693592 }, 1.0 },
			{ "RBreast_skin", "Chest", { 0.106522, 0.334439, 0.936378, -0.28403, 0.912731, -0.293682, -0.95288, -0.234676, 0.192216 }, { 15.0136, -0.774414, -3.693718 }, 1.0 },
			{ "Chest_Rear_Skin", "Chest", { 0.999908, 0.013541, 0.0, -0.013541, 0.999908, -0.0, -0.0, 0.0, 1.0 }, { -0.018502, -1.425966, -1.4e-05 }, 1.0 },
			{ "Chest_Upper_skin", "Chest", { 0.988089, 0.153883, 0.0, -0.153883, 0.988089, -0.0, -0.0, 0.0, 1.0 }, { 6.246947, -1.638941, 0.0 }, 1.0 },
			{ "Neck_Low_skin", "Chest", { 0.950644, -0.310283, -2.7e-05, 0.310283, 0.950644, -1e-06, 2.6e-05, -7e-06, 1.0 }, { 18.85218, -5.214268, -9.7e-05 }, 1.0 },
			{ "Spine2_skin", "SPINE2", { 1.0, -0.0, 0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 4.6e-05, 1e-06, 7e-06 }, 1.0 },
			{ "UpperBelly_skin", "SPINE2", { 0.99537, 0.096115, 0.0, -0.096115, 0.99537, -0.0, -0.0, 0.0, 1.0 }, { 4.459946, 0.130025, -2e-06 }, 1.0 },
			{ "Spine2_Rear_skin", "SPINE2", { 1.0, -0.0, 0.0, 0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { -7.6e-05, -1.464453, 7e-06 }, 1.0 },
			{ "Spine1_skin", "SPINE1", { 1.0, 0.0, -0.0, -0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { 8e-06, -2e-06, 6e-06 }, 1.0 },
			{ "Belly_skin", "SPINE1", { 0.992374, 0.123263, 2e-06, -0.123263, 0.992374, 1e-06, -2e-06, -1e-06, 1.0 }, { -2.029556, 0.253565, 9e-06 }, 1.0 },
			{ "Spine1_Rear_skin", "SPINE1", { 1.0, 0.0, -0.0, -0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { 3.1e-05, -2.445589, 9e-06 }, 1.0 },
			{ "Camera", "Root", { 1.0, 0.0, -0.0, -0.0, 0.0, -1.0, -0.0, 1.0, 0.0 }, { -1.3e-05, 0.03282, 120.4828 }, 1.0 },
			{ "Camera Control", "Root", { 1.0, -0.0, 0.0, 0.0, 1.0, -0.0, -0.0, 0.0, 1.0 }, { -0.0, -0.0, 0.0 }, 1.0 },
			{ "AnimObjectA", "Root", { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 }, { 0.0, 0.0, -0.0 }, 1.0 },
			{ "AnimObjectB", "Root", { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 }, { 0.0, 0.0, -0.0 }, 1.0 },
			{ "CamTargetParent", "Root", { 1.0, 0.0, 0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }, 1.0 },
			{ "CamTarget", "CamTargetParent", { 1.0, 0.0, 0.0, 0.0, 1.0, -0.0, 0.0, 0.0, 1.0 }, { 2e-06, 4.0, 120.4828 }, 1.0 },
			{ "CharacterBumper", "skeleton.nif", { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 }, { -0.608272, 25.30414, 0.0 }, 1.0 },
		};
		constexpr int kRestCount = static_cast<int>(sizeof(kRestTable) / sizeof(kRestTable[0]));

		int Ieq(const char* a, const char* b)
		{
			for (;; ++a, ++b) {
				const int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
				if (ca != cb) return ca - cb;
				if (!ca) return 0;
			}
		}
		bool EndsWithSkin(const char* s)
		{
			const auto l = s ? std::strlen(s) : 0;
			return l > 5 && Ieq(s + l - 5, "_skin") == 0;
		}
		Vec3 U2F(double x, double y, double z) { return { x, z, y }; }  // Unity local direction -> FO4 local direction
		Xform Blend(const Xform& a, const Xform& b, double w)
		{
			if (w <= 0) return a;
			if (w >= 1) return b;
			const Quat qa = FromMat3(a.R);
			Quat       qb = FromMat3(b.R);
			if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0) qb = { -qb.x, -qb.y, -qb.z, -qb.w };
			const Quat q = Normalize(Quat{ qa.x + (qb.x - qa.x) * w, qa.y + (qb.y - qa.y) * w, qa.z + (qb.z - qa.z) * w, qa.w + (qb.w - qa.w) * w });
			return { ToMat3(q), a.t + (b.t - a.t) * w, a.s + (b.s - a.s) * w };
		}
		// arm rig indices (body Rig order = body_model.gen.h kRigs order)
		int RigArm(int s) { return s ? sp::body::kArmR : sp::body::kArmL; }
		int RigElbow(int s) { return s ? sp::body::kElbowR : sp::body::kElbowL; }
		int RigFist(int s) { return s ? sp::body::kFistR : sp::body::kFistL; }
	}

	// ------------------------------------------------------------------------------------------------ rest table
	int             RestCount() { return kRestCount; }
	const RestBone& RestAt(int a_k) { return kRestTable[std::clamp(a_k, 0, kRestCount - 1)]; }
	int             RestIndex(const char* a_name)
	{
		if (!a_name || !*a_name) return -1;
		for (int k = 0; k < kRestCount; ++k)
			if (Ieq(kRestTable[k].name, a_name) == 0) return k;
		return -1;
	}
	Xform RestLocalOf(int k)
	{
		const auto& r = kRestTable[std::clamp(k, 0, kRestCount - 1)];
		Xform       x;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j) x.R.m[i][j] = r.R[i * 3 + j];
		x.t = { r.t[0], r.t[1], r.t[2] };
		x.s = r.s;
		return x;
	}
	Xform RestModelOf(int k)
	{
		Xform chain[64];
		int   n = 0;
		for (int c = k; c >= 0 && n < 64; c = RestIndex(kRestTable[c].parent)) chain[n++] = RestLocalOf(c);
		Xform w{};
		for (int i = n - 1; i >= 0; --i) w = Compose(w, chain[i]);
		return w;
	}

	// ------------------------------------------------------------------------------------------------ helpers
	bool Finite(const Xform& x)
	{
		if (!IsFinite(x.t) || !std::isfinite(x.s) || x.s <= 1e-4 || x.s > 1e4) return false;
		for (const auto& row : x.R.m)
			for (double v : row)
				if (!std::isfinite(v)) return false;
		return std::abs(x.t.x) < 1e7 && std::abs(x.t.y) < 1e7 && std::abs(x.t.z) < 1e7;
	}
	Quat FromTo(const Vec3& a, const Vec3& b)
	{
		const Vec3 u = Normalize(a), v = Normalize(b);
		if (LengthSq(u) < 0.5 || LengthSq(v) < 0.5) return {};
		const double d = std::clamp(Dot(u, v), -1.0, 1.0);
		Vec3         ax = Cross(u, v);
		if (Length(ax) < 1e-9) {
			if (d > 0) return {};
			ax = Normalize(Cross(u, std::abs(u.x) < 0.9 ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 }));
			return AxisAngle(ax, kPi);
		}
		return AxisAngle(Normalize(ax), std::acos(d));
	}
	Quat ScaleRot(const Quat& a_q, double a_w)
	{
		Quat q = Normalize(a_q);
		if (q.w < 0) q = { -q.x, -q.y, -q.z, -q.w };
		const double sn = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
		if (!(sn > 1e-12) || !std::isfinite(sn) || !std::isfinite(a_w)) return {};
		const double ang = 2.0 * std::atan2(sn, q.w);
		return AxisAngle({ q.x / sn, q.y / sn, q.z / sn }, ang * a_w);
	}
	Mat3 AimAxis(const Mat3& a_R, const Vec3& a_localAxis, const Vec3& a_dirWorld, double a_share)
	{
		const Vec3 cur = a_R * a_localAxis;
		if (LengthSq(cur) < 1e-12 || LengthSq(a_dirWorld) < 1e-12) return a_R;
		const Quat q = a_share >= 1.0 ? FromTo(cur, a_dirWorld) : ScaleRot(FromTo(cur, a_dirWorld), std::clamp(a_share, 0.0, 1.0));
		return ToMat3(q) * a_R;
	}

	const char* BoneNameForPart(int a_part, int a_torsoBone)
	{
		using namespace sp::body;
		switch (a_part) {
		case kHead: return "HEAD";
		case kHip: return "Pelvis";
		case kTorso: return a_torsoBone != 0 ? "SPINE2" : "Chest";
		case kArmL: return "LArm_UpperArm";
		case kElbowL: return "LArm_ForeArm1";
		case kArmR: return "RArm_UpperArm";
		case kElbowR: return "RArm_ForeArm1";
		case kLegL: return "LLeg_Thigh";
		case kKneeL: return "LLeg_Calf";
		case kLegR: return "RLeg_Thigh";
		case kKneeR: return "RLeg_Calf";
		case kFistL: return "LArm_Hand";
		case kFistR: return "RArm_Hand";
		default: return nullptr;
		}
	}
	const char* ArmBoneName(int a_side, int a_bone)
	{
		static constexpr const char* kL[kArmBones] = { "LArm_Collarbone", "LArm_UpperArm", "LArm_UpperTwist1", "LArm_UpperTwist2", "LArm_ForeArm1", "LArm_ForeArm2",
			"LArm_ForeArm3", "LArm_Hand" };
		static constexpr const char* kR[kArmBones] = { "RArm_Collarbone", "RArm_UpperArm", "RArm_UpperTwist1", "RArm_UpperTwist2", "RArm_ForeArm1", "RArm_ForeArm2",
			"RArm_ForeArm3", "RArm_Hand" };
		if (a_bone < 0 || a_bone >= kArmBones) return "?";
		return a_side ? kR[a_bone] : kL[a_bone];
	}

	// ------------------------------------------------------------------------------------------------ plan
	bool BuildPlan(Plan& o, const char* const* a_names, const int* a_parents, int a_n, int a_torsoBone)
	{
		o = Plan{};
		if (!a_names || !a_parents || a_n <= 0 || a_n > kMaxBones) return false;
		const int n = a_n;
		o.n = n;
		o.parentFirst = true;
		auto find = [&](const char* nm) {
			for (int i = 0; i < n; ++i)
				if (a_names[i] && Ieq(a_names[i], nm) == 0) return i;
			return -1;
		};
		for (int i = 0; i < n; ++i) {
			o.parent[i] = static_cast<std::int16_t>(a_parents[i]);
			if (a_parents[i] >= i || a_parents[i] < -1) o.parentFirst = false;
			const int k = RestIndex(a_names[i]);
			o.restIdx[i] = static_cast<std::int16_t>(k);
			o.restLocal[i] = k >= 0 ? RestLocalOf(k) : Xform{};
			o.partOf[i] = -1;
			o.kind[i] = kKeep;
			o.armRole[i] = -1;
		}
		int mapped = 0;
		for (int p = 0; p < kPartCount; ++p) {
			o.partBone[p] = -1;
			const char* nm = BoneNameForPart(p, a_torsoBone);
			const int   i = nm ? find(nm) : -1;
			if (i < 0) continue;
			o.partBone[p] = static_cast<std::int16_t>(i);
			o.partOf[i] = static_cast<std::int8_t>(p);
			o.kind[i] = kPart;
			++mapped;
		}
		o.com = find("COM");
		o.pelvis = o.partBone[sp::body::kHip];
		if (o.com >= 0 && o.pelvis >= 0 && o.restIdx[o.pelvis] >= 0 && o.parent[o.pelvis] == o.com) o.kind[o.com] = kCOM;
		static constexpr const char* kDerived[] = { "SPINE1", "SPINE2", "Chest", "Neck", "LArm_Collarbone", "RArm_Collarbone", "LArm_ForeArm2", "LArm_ForeArm3",
			"RArm_ForeArm2", "RArm_ForeArm3", "LArm_UpperTwist1", "LArm_UpperTwist2", "RArm_UpperTwist1", "RArm_UpperTwist2", "LLeg_Foot", "RLeg_Foot", "LLeg_Toe1",
			"RLeg_Toe1" };
		for (const char* nm : kDerived)
			if (const int i = find(nm); i >= 0 && o.kind[i] == kKeep && o.restIdx[i] >= 0) o.kind[i] = kRest;
		{  // R6 STEP 7.7 spine split: COM -> SPINE1 -> SPINE2 -> Chest (= the torso part), all with rest locals
			o.spine1 = find("SPINE1");
			o.spine2 = find("SPINE2");
			const int ch = o.partBone[sp::body::kTorso];
			const int s1 = o.spine1, s2 = o.spine2;
			o.spineOk = o.com >= 0 && s1 >= 0 && s2 >= 0 && ch >= 0 && o.parent[s1] == o.com && o.parent[s2] == s1 && o.parent[ch] == s2 && o.restIdx[s1] >= 0 &&
			            o.restIdx[s2] >= 0 && o.restIdx[ch] >= 0 && o.kind[s1] == kRest && o.kind[s2] == kRest && o.kind[o.com] == kCOM;
		}
		for (int i = 0; i < n; ++i)
			if (o.kind[i] == kKeep && o.restIdx[i] >= 0 && EndsWithSkin(a_names[i])) o.kind[i] = kRest;
		// step 12: the arm chains
		for (int s = 0; s < 2; ++s) {
			bool ok = true;
			for (int b = 0; b < kArmBones; ++b) {
				const int i = find(ArmBoneName(s, b));
				o.arm[s][b] = static_cast<std::int16_t>(i);
				ok = ok && i >= 0 && o.restIdx[i] >= 0;
			}
			if (ok) {
				const auto& a = o.arm[s];
				ok = o.parent[a[kUpper]] == a[kCollar] && o.parent[a[kTwist1]] == a[kUpper] && o.parent[a[kTwist2]] == a[kTwist1] && o.parent[a[kFore1]] == a[kUpper] &&
				     o.parent[a[kFore2]] == a[kFore1] && o.parent[a[kFore3]] == a[kFore2] && o.parent[a[kHand]] == a[kFore3] && o.kind[a[kCollar]] == kRest &&
				     o.kind[a[kUpper]] == kPart && o.kind[a[kTwist1]] == kRest && o.kind[a[kTwist2]] == kRest && o.kind[a[kFore1]] == kPart &&
				     o.kind[a[kFore2]] == kRest && o.kind[a[kFore3]] == kRest && o.kind[a[kHand]] == kPart;
			}
			o.armOk[s] = ok;
			if (ok)
				for (int b = 0; b < kArmBones; ++b) o.armRole[o.arm[s][b]] = static_cast<std::int8_t>(s * 16 + b);
			const int sf = find(s ? "RArm_ShoulderFat_skin" : "LArm_ShoulderFat_skin");
			o.shoulderFat[s] = static_cast<std::int16_t>(ok && sf >= 0 && o.parent[sf] == o.arm[s][kCollar] && o.restIdx[sf] >= 0 && o.kind[sf] == kRest ? sf : -1);
		}
		// step 12: the neck pivot (Neck = derived rest bone, HEAD = the head part, HEAD's parent)
		o.neck = find("Neck");
		o.head = o.partBone[sp::body::kHead];
		o.neckOk = o.neck >= 0 && o.head >= 0 && o.parent[o.head] == o.neck && o.restIdx[o.neck] >= 0 && o.restIdx[o.head] >= 0 && o.kind[o.neck] == kRest &&
		           o.kind[o.head] == kPart;
		return mapped > 0;
	}

	bool BuildRestPlan(Plan& o_plan, int a_torsoBone)
	{
		static const char* names[kMaxBones];
		static int         parents[kMaxBones];
		for (int k = 0; k < kRestCount; ++k) {
			names[k] = kRestTable[k].name;
			parents[k] = RestIndex(kRestTable[k].parent);
		}
		return BuildPlan(o_plan, names, parents, kRestCount, a_torsoBone);
	}

	// ------------------------------------------------------------------------------------------------ params
	Params Params::FromTunables()
	{
		using sp::cfg::Tunable;
		Params p;
		p.translate = static_cast<int>(Tunable("fo4.poseTranslate", 0.0));
		p.restDerived = Tunable("fo4.restDerived", 1.0) != 0.0;
		p.handFromFist = Tunable("fo4.handFromFist", 1.0) != 0.0;
		p.armStretchMax = Tunable("look.armStretchMax", p.armStretchMax);
		p.collarAim = Tunable("look.collarAim", p.collarAim);
		p.neckPivot = static_cast<int>(Tunable("look.neckPivot", 0.0));
		p.neckSwing = Tunable("look.neckSwing", p.neckSwing);
		p.neckSwingMaxDeg = Tunable("look.neckSwingMaxDeg", p.neckSwingMaxDeg);  // s12 fix lane 2
		p.hoverHeadDeg = Tunable("look.hoverHeadDeg", p.hoverHeadDeg);           // s12 fix round 3 lane 4 (the hover rest bow)
		p.hoverBowMax = Tunable("look.hoverBowMax", p.hoverBowMax);
		return p.Sanitised();
	}
	// s12 fix round 3 lane 4: the hover rest bow (pose_map.h HoverRestShare / HoverBow)
	Vec3 HoverBow(const Vec3& a_pelvis, const Vec3& a_chest, const Vec3& a_head, const Vec3& a_right, double a_deg, double a_max)
	{
		const Vec3 zero{ 0, 0, 0 };
		if (!(a_deg > 0) || !(a_max > 0) || !std::isfinite(a_deg) || !std::isfinite(a_max)) return zero;
		const Vec3   hp = a_head - a_pelvis;
		const double L = Length(hp);
		if (!(L > 1.0) || !std::isfinite(L)) return zero;
		const Vec3 u = hp * (1.0 / L);                         // the hip-head line
		Vec3       f = Cross(a_chest - a_pelvis, a_right);       // his front (up x right)
		f = f - u * Dot(f, u);
		const double lf = Length(f);
		if (!(lf > 1e-6) || !std::isfinite(lf)) return zero;
		f = f * (1.0 / lf);
		const double l1 = Dot(a_chest - a_pelvis, u), l2 = Dot(a_head - a_chest, u);
		if (!(l1 > 1.0) || !(l2 > 1.0)) return zero;
		auto ang = [&](double e) { return std::atan2(e, l1) + std::atan2(e, l2); };  // + = the HEAD behind the chest-hip line
		const double want = a_deg * (3.14159265358979323846 / 180.0);
		const double e0 = Dot(a_chest - a_pelvis, f);
		if (!std::isfinite(e0) || ang(e0) >= want) return zero;
		double lo = e0, hi = e0 + a_max;
		if (ang(hi) <= want) return f * a_max;
		for (int k = 0; k < 24; ++k) {
			const double mid = 0.5 * (lo + hi);
			(ang(mid) < want ? lo : hi) = mid;
		}
		const double x = std::clamp(hi - e0, 0.0, a_max);
		const Vec3   o = f * x;
		return (std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z)) ? o : zero;
	}
	Params Params::Sanitised() const
	{
		Params p = *this;
		if (p.translate < 0 || p.translate > 2) p.translate = 0;
		p.armStretchMax = std::isfinite(p.armStretchMax) ? std::clamp(p.armStretchMax, 0.0, 3.0) : 1.15;
		if (p.armStretchMax > 0 && p.armStretchMax < 1.0) p.armStretchMax = 1.0;  // never shorter than the TABS arm
		p.collarAim = std::isfinite(p.collarAim) ? std::clamp(p.collarAim, 0.0, 1.0) : 1.0;
		p.neckPivot = p.neckPivot != 0 ? 1 : 0;
		p.neckSwing = std::isfinite(p.neckSwing) ? std::clamp(p.neckSwing, 0.0, 1.0) : 1.0;
		p.neckSwingMaxDeg = std::isfinite(p.neckSwingMaxDeg) ? std::clamp(p.neckSwingMaxDeg, 0.0, 180.0) : 0.0;  // s12 fix lane 2 (0 = no cap)
		p.hoverHeadDeg = std::isfinite(p.hoverHeadDeg) ? std::clamp(p.hoverHeadDeg, 0.0, 40.0) : 0.0;           // s12 fix round 3 lane 4 (0 = off)
		p.hoverBowMax = std::isfinite(p.hoverBowMax) ? std::clamp(p.hoverBowMax, 0.0, 12.0) : 6.0;
		return p;
	}

	// ------------------------------------------------------------------------------------------------ the TABS arm
	double TabsUpperRest(int a_side, double a_sizeS, double a_upm)
	{
		const auto& j = sp::body::gen::kRigs[RigElbow(a_side ? 1 : 0)].joint;
		const auto& ra = sp::body::gen::kRigs[RigArm(a_side ? 1 : 0)];
		return std::sqrt(j.connAnchor[0] * j.connAnchor[0] + j.connAnchor[1] * j.connAnchor[1] + j.connAnchor[2] * j.connAnchor[2]) * ra.scale * a_sizeS * a_upm;
	}
	double TabsForeRest(int a_side, double a_sizeS, double a_upm)
	{
		const auto& re = sp::body::gen::kRigs[RigElbow(a_side ? 1 : 0)];
		const auto& j = re.joint;
		return std::sqrt(j.anchor[0] * j.anchor[0] + j.anchor[1] * j.anchor[1] + j.anchor[2] * j.anchor[2]) * re.scale * a_sizeS * a_upm;
	}
	double TabsArmRest(double a_sizeS, double a_upm)
	{
		const auto& a = sp::body::gen::kRigs[sp::body::kArmL].pos;
		const auto& e = sp::body::gen::kRigs[sp::body::kElbowL].pos;
		const double dx = e[0] - a[0], dy = e[1] - a[1], dz = e[2] - a[2];
		return std::sqrt(dx * dx + dy * dy + dz * dz) * a_sizeS * a_upm;
	}

	SimArm SimArmPoints(const sp::body::PoseFrame& a_pose, int a_side, double a_sizeS, double a_upm)
	{
		SimArm      o;
		const int   s = a_side ? 1 : 0;
		const int   A = RigArm(s), E = RigElbow(s), F = RigFist(s);
		const auto& P = a_pose.part;
		if (!Finite(P[A]) || !Finite(P[E]) || !Finite(P[F]) || !(a_sizeS > 0) || !(a_upm > 0)) return o;
		const auto& ra = sp::body::gen::kRigs[A];
		const auto& re = sp::body::gen::kRigs[E];
		const auto& j = re.joint;  // the elbow joint: child = Elbow rig (anchor), parent = Arm rig (connAnchor)
		const Vec3  onArm = P[A].t + P[A].R * (U2F(j.connAnchor[0], j.connAnchor[1], j.connAnchor[2]) * (ra.scale * a_sizeS * a_upm));
		const Vec3  onFore = P[E].t + P[E].R * (U2F(j.anchor[0], j.anchor[1], j.anchor[2]) * (re.scale * a_sizeS * a_upm));
		o.shoulder = P[A].t;
		o.elbow = (onArm + onFore) * 0.5;
		o.hand = P[E].t;
		o.fist = P[F].t;
		o.gap = Length(onArm - onFore);
		o.ok = IsFinite(o.elbow) && IsFinite(o.hand) && std::isfinite(o.gap);
		return o;
	}

	void RotateArmChain(sp::body::PoseFrame& a_pose, int a_side, const Quat& a_qUpper, const Quat& a_qFore, double a_sizeS, double a_upm)
	{
		const int s = a_side ? 1 : 0;
		const int A = RigArm(s), E = RigElbow(s), F = RigFist(s);
		auto&     P = a_pose.part;
		if (!Finite(P[A]) || !Finite(P[E]) || !Finite(P[F])) return;
		const auto& ra = sp::body::gen::kRigs[A];
		const auto& re = sp::body::gen::kRigs[E];
		const auto& j = re.joint;
		const Vec3  apL = U2F(j.connAnchor[0], j.connAnchor[1], j.connAnchor[2]) * (ra.scale * a_sizeS * a_upm);
		const Vec3  acL = U2F(j.anchor[0], j.anchor[1], j.anchor[2]) * (re.scale * a_sizeS * a_upm);
		const sp::body::PoseFrame orig = a_pose;
		const Vec3 eArm0 = P[A].t + P[A].R * apL;    // the elbow on the upper arm, before
		const Vec3 eFore0 = P[E].t + P[E].R * acL;   // the elbow on the forearm, before
		const Vec3 gap = eFore0 - eArm0;
		const Quat qU = Normalize(a_qUpper), qF = Normalize(a_qFore);
		P[A].R = ToMat3(qU) * P[A].R;                // the upper arm turns about the shoulder (its pivot)
		const Vec3 eArm1 = P[A].t + P[A].R * apL;
		const Vec3 handOff = P[F].t - P[E].t;         // fist pivot relative to the hand pivot
		P[E].R = ToMat3(qF) * P[E].R;                 // the forearm turns (world rotation as given) and hangs from the new elbow
		P[E].t = eArm1 + gap - P[E].R * acL;
		P[F].R = ToMat3(qF) * P[F].R;
		P[F].t = P[E].t + qF.Rotate(handOff);
		if (!Finite(P[A]) || !Finite(P[E]) || !Finite(P[F])) a_pose = orig;
	}

	// ------------------------------------------------------------------------------------------------ offsets (pure copy of
	// fo4/skeleton CaptureOffsets: the vanilla rest placed at the sim hip, facing the sim, limbs swung onto the TABS rest)
	void CaptureOffsets(const Plan& a_plan, const sp::body::PoseFrame& a_rest, Xform* o_offset, Xform* o_restWorld)
	{
		using namespace sp::body;
		if (!o_offset || !a_rest.valid) return;
		Vec3 right = a_rest.part[kArmR].t - a_rest.part[kArmL].t;
		right.z = 0;
		right = Normalize(right, { 1, 0, 0 });
		const double yaw = std::atan2(right.y, right.x);
		const int    kPel = RestIndex("Pelvis");
		Xform        A;
		A.R = ToMat3(AxisAngle({ 0, 0, 1 }, yaw));
		A.s = a_rest.part[kHip].s;
		const Xform pelModel = kPel >= 0 ? RestModelOf(kPel) : Xform{};
		A.t = a_rest.part[kHip].t - A.R * (pelModel.t * A.s);
		const Vec3 armDirL = Normalize(a_rest.part[kElbowL].t - a_rest.part[kArmL].t, -right);
		const Vec3 armDirR = Normalize(a_rest.part[kElbowR].t - a_rest.part[kArmR].t, right);
		const Vec3 legDirL = Normalize(a_rest.part[kKneeL].t - a_rest.part[kLegL].t, { 0, 0, -1 });
		const Vec3 legDirR = Normalize(a_rest.part[kKneeR].t - a_rest.part[kLegR].t, { 0, 0, -1 });
		struct Swing
		{
			const char* bone;
			const char* child;
			Vec3        dir;
		};
		const Swing swings[] = { { "LArm_UpperArm", "LArm_ForeArm1", armDirL }, { "LArm_ForeArm1", "LArm_Hand", armDirL }, { "LArm_Hand", "LArm_Finger31", armDirL },
			{ "RArm_UpperArm", "RArm_ForeArm1", armDirR }, { "RArm_ForeArm1", "RArm_Hand", armDirR }, { "RArm_Hand", "RArm_Finger31", armDirR },
			{ "LLeg_Thigh", "LLeg_Calf", legDirL }, { "LLeg_Calf", "LLeg_Foot", legDirL }, { "RLeg_Thigh", "RLeg_Calf", legDirR }, { "RLeg_Calf", "RLeg_Foot", legDirR } };
		static Xform tw[kRestCount];  // game thread / test thread only
		for (int k = 0; k < kRestCount; ++k) {
			const int pk = RestIndex(kRestTable[k].parent);
			tw[k] = Compose(pk >= 0 && pk < k ? tw[pk] : A, RestLocalOf(k));
			for (const auto& sw : swings) {
				if (Ieq(kRestTable[k].name, sw.bone) != 0) continue;
				const int ck = RestIndex(sw.child);
				if (ck < 0) break;
				const Vec3 from = Normalize(tw[k].R * RestLocalOf(ck).t, sw.dir);
				tw[k].R = ToMat3(FromTo(from, sw.dir)) * tw[k].R;
				break;
			}
		}
		for (int p = 0; p < kPartCount; ++p) {
			const int i = a_plan.partBone[p];
			if (i < 0) {
				o_offset[p] = Xform{};
				continue;
			}
			Xform restWorld;
			if (const int k = a_plan.restIdx[i]; k >= 0) restWorld = tw[k];
			else if (o_restWorld) restWorld = o_restWorld[i];  // custom bone: the caller's current pose
			o_offset[p] = Relative(a_rest.part[p], restWorld);
		}
	}

	// ------------------------------------------------------------------------------------------------ the mapping
	namespace
	{
		// The pre-step chain of one arm (rotation-only bones at FO4 lengths) and the TABS chain, blended by a_w.
		bool ArmChain(const Plan& P, int s, const Xform& chestW, const std::array<Xform, kPartCount>& W, const MapIn& in, double a_w, Xform* out, ArmDiag& d)
		{
			const auto& idx = P.arm[s];
			auto        rl = [&](int b) -> const Xform& { return P.restLocal[idx[b]]; };
			const int   A = RigArm(s), E = RigElbow(s), F = RigFist(s);
			Xform       old[kArmBones];
			old[kCollar] = Compose(chestW, rl(kCollar));
			{
				const Xform c = Compose(old[kCollar], rl(kUpper));
				old[kUpper] = { W[A].R, c.t, c.s };
			}
			old[kTwist1] = Compose(old[kUpper], rl(kTwist1));
			old[kTwist2] = Compose(old[kTwist1], rl(kTwist2));
			{
				const Xform c = Compose(old[kUpper], rl(kFore1));
				old[kFore1] = { W[E].R, c.t, c.s };
			}
			old[kFore2] = Compose(old[kFore1], rl(kFore2));
			old[kFore3] = Compose(old[kFore2], rl(kFore3));
			{
				const Xform c = Compose(old[kFore3], rl(kHand));
				old[kHand] = { W[F].R, c.t, c.s };
			}
			const SimArm sim = SimArmPoints(*in.pose, s, in.sizeS, in.upm);
			d.sim = sim;
			if (!sim.ok) return false;
			// the sim joints, each segment at most armStretchMax x its TABS rest length (a hitch never draws a giant arm)
			const double uR = TabsUpperRest(s, in.sizeS, in.upm), fR = TabsForeRest(s, in.sizeS, in.upm);
			Vec3         dU = sim.elbow - sim.shoulder, dF = sim.hand - sim.elbow;
			double       lU = Length(dU), lF = Length(dF);
			if (in.p.armStretchMax > 0) {
				const double mU = in.p.armStretchMax * uR, mF = in.p.armStretchMax * fR;
				if (lU > mU && lU > 1e-9) {
					dU = dU * (mU / lU);
					lU = mU;
					d.clamped = true;
				}
				if (lF > mF && lF > 1e-9) {
					dF = dF * (mF / lF);
					lF = mF;
					d.clamped = true;
				}
			}
			Vec3 Ej = sim.shoulder + dU;
			const Vec3 S = sim.shoulder, H = Ej + dF;
			// the One Punch Man killing punch (punchAnim.killPose, jab off): the arm drawn straight from the shoulder to the hand,
			// the elbow on that line at the upper arm's share; the hand stays on the sim hand (the fist you see is the hit)
			if (const double sw = std::isfinite(in.pose->straightW[s]) ? std::clamp(static_cast<double>(in.pose->straightW[s]), 0.0, 1.0) : 0.0; sw > 0.0 && lU + lF > 1e-6) {
				const Vec3 onLine = S + (H - S) * (lU / (lU + lF));
				Ej = Ej + (onLine - Ej) * sw;
				dU = Ej - S;
				dF = H - Ej;
				lU = Length(dU);
				lF = Length(dF);
			}
			Xform      nw[kArmBones];
			// the Collarbone swings (minimal rotation, its twist kept) onto the drawn shoulder
			nw[kCollar] = old[kCollar];
			d.shOff = Length(S - old[kUpper].t);
			if (in.p.collarAim > 0) {
				const Quat q = ScaleRot(FromTo(old[kUpper].t - old[kCollar].t, S - old[kCollar].t), in.p.collarAim);
				nw[kCollar].R = ToMat3(q) * old[kCollar].R;
			}
			{  // how far the drawn shoulder is from the Collarbone vs its FO4 rest (the shoulder pad bone shares that stretch)
				const double rc = Length(rl(kUpper).t) * old[kCollar].s;
				d.kC = rc > 1e-6 ? std::clamp(Length(S - old[kCollar].t) / rc, 0.5, 2.0) : 1.0;
			}
			// upper arm: on the shoulder, its axis (toward ForeArm1) on the elbow joint; the twist bones share the stretch
			const double sU = Compose(nw[kCollar], rl(kUpper)).s;
			nw[kUpper] = { AimAxis(W[A].R, rl(kFore1).t, dU), S, sU };
			const double restU = Length(rl(kFore1).t) * sU;
			const double kU = restU > 1e-6 ? lU / restU : 1.0;
			Xform        t1 = rl(kTwist1), t2 = rl(kTwist2);
			t1.t = t1.t * kU;
			t2.t = t2.t * kU;
			nw[kTwist1] = Compose(nw[kUpper], t1);
			nw[kTwist2] = Compose(nw[kTwist1], t2);
			// forearm: on the elbow joint, the rest line ForeArm1 -> Hand on the hand; ForeArm2 / 3 (and so the Hand's own local)
			// share the stretch
			const Xform  f23 = Compose(rl(kFore2), Compose(rl(kFore3), rl(kHand)));
			const double sF = Compose(nw[kUpper], rl(kFore1)).s;
			nw[kFore1] = { AimAxis(W[E].R, f23.t, dF), Ej, sF };
			const double restF = Length(f23.t) * sF;
			const double kF = restF > 1e-6 ? lF / restF : 1.0;
			Xform        l2 = rl(kFore2), l3 = rl(kFore3);
			l2.t = l2.t * kF;
			l3.t = l3.t * kF;
			nw[kFore2] = Compose(nw[kFore1], l2);
			nw[kFore3] = Compose(nw[kFore2], l3);
			nw[kHand] = { W[F].R, H, Compose(nw[kFore3], rl(kHand)).s };
			for (int b = 0; b < kArmBones; ++b) {
				out[b] = Blend(old[b], nw[b], a_w);
				if (!Finite(out[b])) return false;
			}
			d.kU = kU;
			d.kF = kF;
			d.w = a_w;
			d.active = true;
			d.shoulder = out[kUpper].t;
			d.elbow = out[kFore1].t;
			d.hand = out[kHand].t;
			return true;
		}
	}

	bool MapPose(const Plan& Pl, const MapIn& in, MapOut& o)
	{
		using namespace sp::body;
		o.badBone = -1;
		o.nanFallbacks = 0;
		o.neckActive = false;
		o.neckSwingDeg = 0;
		o.neckFullDeg = 0;
		o.neckCapped = false;
		o.spineQFault = false;
		o.spineWrites = 0;
		o.hoverBowU = 0;
		o.arm[0] = o.arm[1] = ArmDiag{};
		if (!in.pose || !in.offset || !in.local || !in.world) return false;
		const auto& pose = *in.pose;
		const int   n = Pl.n;
		if (n <= 0 || n > kMaxBones) return false;
		std::array<Xform, kPartCount> W{};
		for (int p = 0; p < kPartCount; ++p) {
			W[p] = Compose(pose.part[p], in.offset[p]);
			if (Pl.partBone[p] >= 0 && !Finite(W[p])) {
				o.badBone = Pl.partBone[p];
				return false;
			}
		}
		const double split = (Pl.spineOk && std::isfinite(in.split)) ? std::clamp(in.split, 0.0, 0.9) : 0.0;
		Quat         spineQ{};
		bool         haveSpineQ = false;
		// step 12: the TABS arm share (0 = the pre-step arms exactly) and the neck pivot
		double armW = 0.0;
		if (in.p.translate == 1 && in.p.handFromFist && in.p.restDerived && !in.armsOff && std::isfinite(pose.tabsArmW)) armW = std::clamp(static_cast<double>(pose.tabsArmW), 0.0, 1.0);
		// fix lane 4: neckW is the pivot's SHARE (0..1; runtime NeckShare): 0 = the pre-step head exactly, 1 = the pivot exactly
		const double neckShare = std::isfinite(pose.neckW) ? std::clamp(static_cast<double>(pose.neckW), 0.0, 1.0) : 0.0;
		const bool neckOn = in.p.neckPivot != 0 && in.p.translate != 2 && in.p.restDerived && Pl.neckOk && !in.neckOff && neckShare > 1e-3;
		Xform      chain[2][kArmBones];
		bool       chainOn[2]{ false, false };
		Xform      neckXf{}, headXf{};
		bool       neckReady = false;
		// s12 fix round 3 lane 4 (look.hoverHeadDeg): the TABS hover rest bow - the Chest forward, the Neck back by the same offset
		// (HEAD and Pelvis stay where they were drawn); only on the vanilla chain Chest -> Neck -> HEAD
		const double hoverW = std::isfinite(pose.hoverW) ? std::clamp(static_cast<double>(pose.hoverW), 0.0, 1.0) : 0.0;
		const int    chestIdx = Pl.partBone[kTorso];
		// (with the step-12 TABS look only: fo4.poseTranslate 0 - the old look's key back - draws the pre-step writer exactly, MAP-03)
		const bool   bowOn = in.p.hoverHeadDeg > 0 && hoverW > 1e-3 && in.p.translate == 1 && chestIdx >= 0 && chestIdx < n && Pl.neck >= 0 && Pl.neck < n &&
		                     Pl.head >= 0 && Pl.head < n && Pl.parent[Pl.neck] == chestIdx && Pl.parent[Pl.head] == Pl.neck && Pl.partBone[kHip] >= 0;
		Vec3         bow{ 0, 0, 0 };
		bool         bowSet = false;

		for (int i = 0; i < n; ++i) {
			o.write[i] = o.writeLocal[i] = false;
			const int par = Pl.parent[i];
			Kind      k = Pl.kind[i];
			if (k == kPart) {
				const int p = Pl.partOf[i];
				if ((p == kFistL || p == kFistR) && !in.p.handFromFist) k = Pl.restIdx[i] >= 0 ? kRest : kKeep;
			} else if (k == kRest && !in.p.restDerived) {
				k = kKeep;
			}
			const bool parentDirty = par >= 0 && par < i && o.write[par];
			const int  role = Pl.armRole[i];
			const int  side = role >= 0 ? role / 16 : -1, bone = role >= 0 ? role % 16 : -1;
			const bool chainBone = role >= 0 && armW > 0.0 && Pl.armOk[side];
			const bool neckBone = neckOn && (i == Pl.neck || i == Pl.head);
			if (k == kKeep && !parentDirty && !chainBone && !neckBone) continue;

			const Xform pw = par >= 0 && par < n ? (o.write[par] ? o.world[par] : in.world[par]) : in.rootWorld;
			Xform       w;
			bool        writeLocal = true;
			bool        done = false;
			if (chainBone) {
				if (bone == kCollar) {
					chainOn[side] = ArmChain(Pl, side, pw, W, in, armW, chain[side], o.arm[side]);
					if (!chainOn[side]) {
						++o.nanFallbacks;
						o.arm[side].active = false;
					}
				}
				if (chainOn[side]) {
					w = chain[side][bone];
					done = true;
				}
			}
			if (!done && neckBone) {
				if (i == Pl.neck) {
					// the head turns about the neck base (the TABS neck pivot): HEAD = neck base + R_head * (its rest offset in the
					// head frame); the Neck bone swings (no twist) to aim at it
					Xform neckRest = Compose(pw, Pl.restLocal[Pl.neck]);
					if (bowSet) neckRest.t = neckRest.t - bow;  // s12 fix round 3 lane 4: the neck base stays where it was (the Chest bowed)
					const Xform& hl = Pl.restLocal[Pl.head];
					const Vec3   v = hl.t;                    // neck base -> HEAD, Neck-local
					const Vec3   wv = hl.R.Transposed() * v;  // the same, HEAD-local
					const Mat3&  RH = W[kHead].R;
					const Vec3   d0 = neckRest.R * v;
					// s12 fix integrator r1: fix lane 4's share (look.neckFightOnly) and fix lane 2's cap (look.neckSwingMaxDeg) composed ON
					// the neck's sphere: the pivot rotation qFull (rest offset -> the step-12 pivot point) is taken at the share k (0 = the
					// rigid pre-step head, 1 = the pivot exactly), k capped so the Neck swings at most neckSwingMaxDeg; HEAD = neck base +
					// R(k) x the rest offset in world scale = the Neck's own tip at every share (lane 4's straight chord glide put HEAD up
					// to 5.6 u inside the neck during a glide with a big head turn: desktop NECK-CAP seam 2.7-5.6 u, now 0)
					// s12 fix lane 2 (REVIEW_g 2, look.neckSwingMaxDeg): the pivot takes at most the cap of the head-vs-chest swing; HEAD keeps the
					// sim head rotation RH - the rest of the turn is at the HEAD joint, as before step 12 (in game the uncapped Neck took the
					// whole turn: p99 64, max 133 deg, the fold at the neck base)
					const Vec3   headPivot = neckRest.t + RH * (wv * neckRest.s);
					const double fullAsk = AngleDeg(d0, headPivot - neckRest.t);
					double       kSh = neckShare >= 1.0 ? 1.0 : neckShare;
					o.neckFullDeg = std::isfinite(fullAsk) ? fullAsk * kSh : fullAsk;
					if (in.p.neckSwingMaxDeg > 0 && std::isfinite(fullAsk) && fullAsk * kSh > in.p.neckSwingMaxDeg) {
						kSh = in.p.neckSwingMaxDeg / fullAsk;
						o.neckCapped = true;
					}
					const Quat qFull = FromTo(d0, headPivot - neckRest.t);
					const Quat qPivot = kSh >= 1.0 ? qFull : ScaleRot(qFull, kSh);
					const Vec3 headT = kSh >= 1.0 ? headPivot : neckRest.t + ToMat3(qPivot) * (d0 * neckRest.s);
					const Quat q = ScaleRot(qPivot, in.p.neckSwing);
					neckXf = { ToMat3(q) * neckRest.R, neckRest.t, neckRest.s };
					headXf = { RH, headT, neckRest.s * hl.s };
					if (Finite(neckXf) && Finite(headXf)) {
						neckReady = true;
						o.neckActive = true;
						o.neckSwingDeg = AngleDeg(d0, ToMat3(q) * d0);  // the drawn Neck swing (after the cap and the share)
					} else {
						++o.nanFallbacks;
					}
				}
				if (neckReady) {
					w = i == Pl.neck ? neckXf : headXf;
					done = true;
				}
			}
			if (!done) {
				switch (k) {
				case kKeep:
					w = Compose(pw, in.local[i]);
					writeLocal = false;
					break;
				case kRest:
					w = Compose(pw, Pl.restLocal[i]);
					for (int s = 0; s < 2; ++s)  // step 12: the shoulder pad bone shares the Collarbone -> shoulder stretch
						if (i == Pl.shoulderFat[s] && chainOn[s] && o.arm[s].active) {
							Xform l = Pl.restLocal[i];
							const double k = 1.0 + (o.arm[s].kC - 1.0) * o.arm[s].w;
							l.t = l.t * k;
							w = Compose(pw, l);
						}
					if (split > 0 && (i == Pl.spine1 || i == Pl.spine2)) {  // R6 STEP 7.7: the spine takes its share of the turn
						if (!haveSpineQ) {  // at SPINE1 (parent-first): pw = this pass's COM world
							haveSpineQ = true;
							const Mat3 rigid = pw.R * Pl.restLocal[Pl.spine1].R * Pl.restLocal[Pl.spine2].R * Pl.restLocal[Pl.partBone[kTorso]].R;
							spineQ = ScaleRot(FromMat3(W[kTorso].R * rigid.Transposed()), 0.5 * split);
							if (!std::isfinite(spineQ.x) || !std::isfinite(spineQ.y) || !std::isfinite(spineQ.z) || !std::isfinite(spineQ.w)) {
								spineQ = {};
								o.spineQFault = true;
							}
						}
						const Xform w0 = w;
						w.R = ToMat3(spineQ) * w.R;
						if (!Finite(w)) w = w0;
						else if (i == Pl.spine2) ++o.spineWrites;
					}
					break;
				case kPart:
					{
						// rotation from the sim; scale from the parent chain; position from the sim for the Pelvis (and every
						// part bone with fo4.poseTranslate 2, the old probe), else from the FO4 bone length
						const Xform& full = W[Pl.partOf[i]];
						const Xform  l = Pl.restIdx[i] >= 0 ? Pl.restLocal[i] : in.local[i];
						const Xform  c = Compose(pw, l);
						w = { full.R, (in.p.translate != 2 && i != Pl.pelvis) ? c.t : full.t, c.s };
					}
					break;
				case kCOM:
					{
						// COM such that Compose(COM, restLocal(Pelvis)) == the sim's pelvis (scale from Root)
						const Xform& pel = W[kHip];
						const Xform& lp = Pl.restLocal[Pl.pelvis];
						const Xform  l = Pl.restIdx[i] >= 0 ? Pl.restLocal[i] : in.local[i];
						w.s = pw.s * l.s;
						w.R = pel.R * lp.R.Transposed();
						w.t = pel.t - w.R * (lp.t * w.s);
					}
					break;
				default:  // test-pose kinds never appear in a body plan
					w = Compose(pw, in.local[i]);
					writeLocal = false;
					break;
				}
			}
			// s12 fix round 3 lane 4 (look.hoverHeadDeg): the hover rest bow - the Chest moves along his front until the HEAD sits
			// hoverHeadDeg behind the chest-hip line (x the share); the Neck (and so the HEAD) takes the opposite offset. HEAD predicted
			// as the plain chain (the neck pivot's share is 0 at rest); the Pelvis = the sim hip (kPart: its position is the sim's)
			if (bowOn && i == chestIdx && Finite(w)) {
				const Xform& nl = Pl.restIdx[Pl.neck] >= 0 ? Pl.restLocal[Pl.neck] : in.local[Pl.neck];
				const Xform& hl = Pl.restIdx[Pl.head] >= 0 ? Pl.restLocal[Pl.head] : in.local[Pl.head];
				const Vec3   h0 = Compose(Compose(w, nl), hl).t;
				const Vec3   b = HoverBow(W[kHip].t, w.t, h0, pose.part[kArmR].t - pose.part[kArmL].t, in.p.hoverHeadDeg, in.p.hoverBowMax) * hoverW;
				if (std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.z) && Length(b) > 1e-6) {
					bow = b;
					bowSet = true;
					w.t = w.t + bow;
					writeLocal = true;
					o.hoverBowU = Length(bow);
				}
			} else if (bowSet && i == Pl.neck && !neckReady) {
				w.t = w.t - bow;
				writeLocal = true;
			}
			if (!Finite(w)) {
				o.badBone = i;
				return false;
			}
			o.world[i] = w;
			if (writeLocal) o.local[i] = Relative(pw, w);
			o.write[i] = true;
			o.writeLocal[i] = writeLocal;
		}
		return true;
	}
}

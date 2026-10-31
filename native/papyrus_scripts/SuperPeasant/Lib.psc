Scriptname SuperPeasant:Lib Hidden
{Super Peasant pure maths (contract.json lib.functions). Global functions only: no state, no native calls except
Math (and Debug.TraceUser in Log). Angles are degrees like Papyrus Math.sin/cos/atan. Heading 0 = +Y (north),
90 = +X (east), clockwise seen from above (the GetAngleZ convention). Units are Fallout 4 units (64.18 u per TABS
metre, spec_design 1.1). test\twin.py is the Python twin of every function here; test\golden_vectors.json holds
its results.}

; ---------------------------------------------------------------------------------------------------- logging

; One log line 'SP|<LEVEL>|<asSource>|<asMsg>' to <MyGames>\Logs\Script\User\SuperPeasant.0.log (the Controller
; opens it with Debug.OpenUserLog). Levels LOG_ERR 0, LOG_WARN 1, LOG_INFO 2, LOG_DBG 3. ERR and WARN always,
; INFO when aiVerbosity (SP_Debug) >= 1, DBG when aiVerbosity >= 2.
Function Log(int aiVerbosity, int aiLevel, string asSource, string asMsg) global
	string lvl = "DBG"
	If aiLevel <= 0
		lvl = "ERR"
	ElseIf aiLevel == 1
		lvl = "WARN"
	ElseIf aiLevel == 2
		If aiVerbosity < 1
			Return
		EndIf
		lvl = "INFO"
	ElseIf aiVerbosity < 2
		Return
	EndIf
	Debug.TraceUser("SuperPeasant", "SP|" + lvl + "|" + asSource + "|" + asMsg)
EndFunction

; ---------------------------------------------------------------------------------------------------- basics

float Function Clamp(float x, float lo, float hi) global
	If x < lo
		Return lo
	ElseIf x > hi
		Return hi
	EndIf
	Return x
EndFunction

float Function Dist3(float dx, float dy, float dz) global
	Return Math.sqrt(dx * dx + dy * dy + dz * dz)
EndFunction

; Heading (GetAngleZ convention) of the direction (dx, dy). (0, 0) gives 0.
float Function HeadingTo(float dx, float dy) global
	If dx == 0.0 && dy == 0.0
		Return 0.0
	EndIf
	If dy == 0.0
		If dx > 0.0
			Return 90.0
		EndIf
		Return 270.0
	EndIf
	float a = Math.atan(dx / dy)
	If dy < 0.0
		a += 180.0
	ElseIf dx < 0.0
		a += 360.0
	EndIf
	Return a
EndFunction

; ---------------------------------------------------------------------------------------------------- dash speed

; TABS fighting build-up (CMB 2.2 x 64.18 u/m): 366, 1142, 1592, 2779, 3472, 4428 u/s at t = 0.10 ... 0.35 s,
; then the steady mean 4197 from 0.40 s, linear in between, never below the single-dash first value 931 u/s.
; t = seconds since he started engaging this target. vmax = SP_DashSpeedMax (<= 0 means the default 4197);
; the whole curve scales by vmax / 4197.
float Function DashSpeed(float t, float vmax) global
	If vmax <= 0.0
		vmax = 4197.0
	EndIf
	float c
	If t <= 0.10
		c = 366.0
	ElseIf t >= 0.40
		c = 4197.0
	ElseIf t < 0.15
		c = 366.0 + (t - 0.10) / 0.05 * (1142.0 - 366.0)
	ElseIf t < 0.20
		c = 1142.0 + (t - 0.15) / 0.05 * (1592.0 - 1142.0)
	ElseIf t < 0.25
		c = 1592.0 + (t - 0.20) / 0.05 * (2779.0 - 1592.0)
	ElseIf t < 0.30
		c = 2779.0 + (t - 0.25) / 0.05 * (3472.0 - 2779.0)
	ElseIf t < 0.35
		c = 3472.0 + (t - 0.30) / 0.05 * (4428.0 - 3472.0)
	Else
		c = 4428.0 + (t - 0.35) / 0.05 * (4197.0 - 4428.0)
	EndIf
	If c < 931.0
		c = 931.0
	EndIf
	Return c * vmax / 4197.0
EndFunction

; ---------------------------------------------------------------------------------------------------- guarding

; Priority ring by distance from the guard centre (the player or his wait spot): a threat about to hit the player
; (< 192 u, the L4D2 3 m "THREAT" ring) x10, then the TABS-port rings 4 / 8 / 14 m = 257 / 513 / 898 u.
float Function RingFactor(float distToCentre) global
	If distToCentre < 192.0
		Return 10.0
	ElseIf distToCentre < 257.0
		Return 4.0
	ElseIf distToCentre < 513.0
		Return 2.0
	ElseIf distToCentre < 898.0
		Return 1.25
	EndIf
	Return 1.0
EndFunction

; TABS target score (higher is better): -distance to him / priority. Priority = ring x (4 for the player's
; attacker) x (0.15 when out of sight).
float Function Score(float distToHim, float distToCentre, bool isAttacker, bool visible) global
	float pr = RingFactor(distToCentre)
	If isAttacker
		pr *= 4.0
	EndIf
	If !visible
		pr *= 0.15
	EndIf
	Return -distToHim / pr
EndFunction

; Guard radius: 1284 u (20 m) while the player stands, 770 u (12 m) while the player moves faster than 128 u/s.
float Function GuardRadius(float playerSpeed) global
	If playerSpeed > 128.0
		Return 770.0
	EndIf
	Return 1284.0
EndFunction

; ---------------------------------------------------------------------------------------------------- punches

; Impact factor k from his closing speed: k = 2 from 94 u/s on a torso (CMB 3.2), between 1 and 2.
float Function PunchK(float dashSpeed) global
	Return Clamp(dashSpeed / 94.0, 1.0, 2.0)
EndFunction

; Launch of a punched target. (dx, dy, dz) = target chest minus his fist/shoulder; heading = his facing.
; Aims 32 u (0.5 m) through the target along his facing, lifts to at least 444 u/s upward, speed
; 2721 * sqrt(k / 2) * flingScale u/s (1925 at k = 1, 2721 at k = 2). Returns [ux, uy, uz, speed], unit vector.
float[] Function FlingVector(float dx, float dy, float dz, float heading, float k, float flingScale) global
	k = Clamp(k, 1.0, 2.0)
	float ax = dx + 32.0 * Math.sin(heading)
	float ay = dy + 32.0 * Math.cos(heading)
	float az = dz
	float n = Math.sqrt(ax * ax + ay * ay + az * az)
	If n < 1.0
		ax = Math.sin(heading)
		ay = Math.cos(heading)
		az = 0.0
		n = 1.0
	EndIf
	float ux = ax / n
	float uy = ay / n
	float uz = az / n
	float speed = 2721.0 * Math.sqrt(k / 2.0) * flingScale
	float[] r = new float[4]
	If speed <= 0.0
		r[0] = ux
		r[1] = uy
		r[2] = uz
		r[3] = 0.0
		Return r
	EndIf
	float minUz = 444.0 / speed
	If minUz > 1.0
		minUz = 1.0
	EndIf
	If uz < minUz
		float h = Math.sqrt(ux * ux + uy * uy)
		uz = minUz
		If h > 0.000001
			float s = Math.sqrt(1.0 - uz * uz) / h
			ux *= s
			uy *= s
		Else
			ux = 0.0
			uy = 0.0
		EndIf
	EndIf
	r[0] = ux
	r[1] = uy
	r[2] = uz
	r[3] = speed
	Return r
EndFunction

; ---------------------------------------------------------------------------------------------------- flight

; Float-down sink speed: 135 u/s, 359 u/s while higher than 360 u above the landing level (BOD 7.2c).
float Function SinkSpeed(float heightAbove) global
	If heightAbove > 360.0
		Return 359.0
	EndIf
	Return 135.0
EndFunction

; A spot dist units beside (px, py, pz) relative to heading. side: 0 right, 1 left, 2 behind, 3 in front
; (anything else = right). Returns [x, y, z].
float[] Function SideSpot(float px, float py, float pz, float heading, float dist, int side) global
	float a = heading + 90.0
	If side == 1
		a = heading + 270.0
	ElseIf side == 2
		a = heading + 180.0
	ElseIf side == 3
		a = heading
	EndIf
	float[] r = new float[3]
	r[0] = px + dist * Math.sin(a)
	r[1] = py + dist * Math.cos(a)
	r[2] = pz
	Return r
EndFunction

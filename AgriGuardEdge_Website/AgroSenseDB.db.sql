BEGIN TRANSACTION;
CREATE TABLE IF NOT EXISTS "chats" (
	"id"	TEXT UNIQUE,
	"userID"	INTEGER,
	"chatData"	TEXT,
	PRIMARY KEY("id"),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS "espDetails" (
	"macADD"	TEXT NOT NULL DEFAULT '3C:8A:1F:76:FB:C4' UNIQUE,
	"userID"	INTEGER NOT NULL UNIQUE,
	"lastActive"	DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),
	"battery"	INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY("macADD"),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS "esp_logs" (
	"entry_id"	INTEGER,
	"espID"	TEXT NOT NULL DEFAULT '3C:8A:1F:76:FB:C4',
	"temperature"	FLOAT NOT NULL,
	"humidity"	FLOAT NOT NULL,
	"relay"	INTEGER NOT NULL DEFAULT 0,
	"moisture"	INTEGER,
	"water_level"	INTEGER,
	"entry_timestamp"	DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),
	PRIMARY KEY("entry_id" AUTOINCREMENT),
	FOREIGN KEY("espID") REFERENCES "espDetails"("macADD")
);
CREATE TABLE IF NOT EXISTS "farmDetails" (
	"id"	INTEGER,
	"userID"	INTEGER NOT NULL UNIQUE,
	"espID"	TEXT NOT NULL DEFAULT '3C:8A:1F:76:FB:C4' UNIQUE,
	"location"	TEXT NOT NULL,
	"totalArea"	INTEGER NOT NULL,
	"soilType"	TEXT NOT NULL,
	"cropType"	TEXT NOT NULL,
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("espID") REFERENCES "espDetails"("macADD"),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS "irrigation_rules" (
	"id"	INTEGER NOT NULL,
	"espID"	TEXT NOT NULL DEFAULT '3C:8A:1F:76:FB:C4',
	"ActiveStart"	DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),
	"ActiveEnd"	DATETIME,
	"Rule"	TEXT NOT NULL,
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("espID") REFERENCES "espDetails"("macADD")
);
CREATE TABLE IF NOT EXISTS "notifications" (
	"id"	INTEGER NOT NULL,
	"userID"	INTEGER NOT NULL,
	"title"	TEXT NOT NULL,
	"description"	TEXT,
	"category"	TEXT NOT NULL DEFAULT 'info' CHECK("category" IN ('info', 'warn', 'success', 'danger')),
	"dateTime"	DATETIME DEFAULT (datetime('now', 'localtime')),
	"read"	INTEGER NOT NULL DEFAULT 0 CHECK("read" IN (0, 1)),
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS "userDetails" (
	"id"	INTEGER NOT NULL,
	"userName"	TEXT NOT NULL,
	"emailId"	TEXT NOT NULL UNIQUE,
	"oAuth"	TEXT UNIQUE,
	"password"	TEXT NOT NULL,
	"espID"	TEXT DEFAULT '3C:8A:1F:76:FB:C4',
	"profilePic"	BLOB,
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("espID") REFERENCES "espDetails"("macADD")
);
INSERT INTO "chats" ("id","userID","chatData") VALUES ('1775246746641',1,'{"messages": [{"sender": "bot", "text": "Hello! I\u2019m Krishimitra \ud83c\udf31 How can I help?"}, {"sender": "user", "text": "hello there, how is it going?"}, {"sender": "bot", "text": "Hello! I\u2019m doing well, thank you. I see you have a small grass farm on red soil. Your recent sensor readings show stable temperature (28\u00b0C) and humidity (54%), but moisture is at 0% \u2014 which is unusual for grass. How can I assist you with your farm today?"}, {"sender": "user", "text": "are there any irrigation rules active right now?"}, {"sender": "bot", "text": "There are currently **no active irrigation rules** set for your farm.  \n\nYour moisture readings show 0% consistently, which is unusual for grass \u2014 you may want to check sensor placement or consider setting up an irrigation rule if the soil is indeed dry.  \nWould you like help creating one based on your crop type and soil conditions?"}, {"sender": "user", "text": "Yes"}, {"sender": "bot", "text": "Great. Let\u2019s set up a basic irrigation rule for your grass on red soil.\n\nSince red soil drains quickly and your moisture sensors show 0%, I\u2019d recommend:\n\n**Suggested Rule:**  \n- Start irrigation when soil moisture falls below **20%**.\n- Stop irrigation when moisture reaches **40%**.\n- Water preferably in the **early morning** (5\u20137 AM) to reduce evaporation losses.\n\nWould you like me to configure this rule for your farm on AgroSense, or do you want to adjust the thresholds first?  \n\nAlso, since moisture is reading 0%, double-check that your soil moisture sensor is properly placed in the root zone and functioning correctly."}, {"sender": "user", "text": "any rules active now?"}, {"sender": "bot", "text": "Currently, **no irrigation rules are active**.\n\nThe only listed rule is a *manual watering* scheduled for **Saturdays from 01:45 to 01:50**, which is not running right now (outside that time window).\n\nGiven your persistent 0% moisture readings, I strongly recommend:\n1.  Verifying your soil moisture sensor is properly installed and functional.\n2.  Setting up an automated rule (e.g., trigger irrigation when moisture < 20%) to maintain healthy grass.\n\nYou can create or adjust rules on the [Auto-Irrigation](/auto-irrigation) page."}, {"sender": "user", "text": "what pestiside should i use"}, {"sender": "bot", "text": "For grass on red soil, common pests include **armyworms, cutworms, chafer grubs, and fungal issues** like brown patch or dollar spot.  \n\n**General recommendations:**  \n- **For insects (grubs, caterpillars):** Use a broad-spectrum insecticide like **Chlorpyriphos** or **Lambda-cyhalothrin** (follow label directions).  \n- **For fungal diseases:** Apply **Carbendazim** or **Mancozeb** as a preventive spray.  \n- **Organic/natural alternatives:** Neem oil, garlic-chilli spray, or introducing beneficial nematodes for grubs.  \n\n**Important first steps:**  \n1. **Identify the pest/disease accurately** \u2014 check for chewed blades, brown patches, or larvae in soil.  \n2. **Improve grass health** \u2014 proper watering (avoid overwatering) and fertilization can reduce pest susceptibility.  \n3. **Spot-treat** affected areas before full-scale application.\n\nWould you like help identifying symptoms or adjusting irrigation to prevent fungal growth?"}, {"sender": "user", "text": "i  want to plant sugarcane  what fertilizers should i use and how should i irrigate?"}, {"sender": "bot", "text": "Excellent choice! Sugarcane is a major cash crop suitable for your region (Maharashtra) and red soil. Here\u2019s a concise guide:\n\n### \ud83c\udf31 **Fertilizer Recommendations for Sugarcane on Red Soil**\nRed soil is often low in organic matter, nitrogen, and phosphorus. A balanced fertilizer program is crucial.\n\n| Growth Stage | Fertilizer (per acre) | Key Notes |\n| :--- | :--- | :--- |\n| **Basal Application (at planting)** | 10\u201312 tonnes Farmyard Manure (FYM) or compost<br>**NPK:** 60:60:60 kg (N:P\u2082O\u2085:K\u2082O) | Incorporate well into soil to improve structure & water retention. |\n| **First Top-Dressing (30\u201345 days after planting)** | 30\u201340 kg Urea (for N) | Apply after first irrigation or rain. |\n| **Second Top-Dressing (90\u2013120 days)** | 30\u201340 kg Urea + 25 kg Potash (if needed) | Critical for tillering and stalk growth. |\n| **Micronutrients** | Zinc Sulphate (25 kg/ha) & Ferrous Sulphate (if yellowing appears) | Red soils can be deficient in zinc & iron. |\n\n**Organic Alternative:** Apply well-decomposed press mud (sugar factory waste) or green manure (like sunn hemp) before planting.\n\n### \ud83d\udca7 **Irrigation Schedule for Sugarcane**\nSugarcane needs ample but well-managed water. **Avoid waterlogging.**\n\n| Growth Phase | Irrigation Frequency (approx.) | Key Points |\n| :--- | :--- | :--- |\n| **Germination & Establishment (0\u201360 days)** | Light, frequent irrigations every 5\u20137 days | Maintain consistent soil moisture. |\n| **Tillering & Grand Growth (60\u2013180 days)** | Heavy irrigation every 10\u201312 days | Peak water demand period. |\n| **Ripening (180\u2013270 days)** | Reduce to every 15\u201320 days | Stress slightly to boost sugar accumulation. |\n\n**Method:** **Drip irrigation** is highly recommended for water efficiency and higher yield on red soil.\n\n### \ud83d\udccc **Immediate Steps for Your Farm**\n1.  **Test Soil:** Conduct a soil test for precise nutrient recommendations.\n2.  **Prepare Land:** Deep ploughing (30\u201345 cm) to break hardpans in red soil.\n3.  **Adjust Irrigation:** Since you\u2019re switching from grass, you\u2019ll need to **update your irrigation rules** on the [Auto-Irrigation](/auto-irrigation) page for sugarcane\u2019s needs.\n4.  **Monitor:** Use your moisture sensors to keep soil moisture between **60-80%** during active growth phases.\n\nWould you like help designing specific drip irrigation schedules or interpreting a soil test report?"}, {"sender": "user", "text": "how can i cook burger"}, {"sender": "bot", "text": "I am Krishimitra, an agricultural assistant. I don''t have information on cooking or recipes, but I can help you with your farm.\n\nYou were just asking about planting sugarcane on your red soil plot. Would you like to continue discussing fertilizer blends, irrigation setup, or any other aspect of sugarcane cultivation?"}, {"sender": "user", "text": "what is happening currently ??"}, {"sender": "bot", "text": "Based on your farm details and latest sensor data, here\u2019s what\u2019s happening **currently**:\n\n### \ud83d\udcca **Current Sensor Status (as of 12:04 PM)**\n- **Temperature:** 31.0\u00b0C  \n- **Humidity:** 43%  \n- **Soil Moisture:** 62%  \n- **Water Level:** Fluctuating (146 in latest reading)\n\n### \ud83c\udf3e **Farm Status**\n- **Current crop:** Grass (on 1 acre of red soil)  \n- **Irrigation rules:** None active  \n- **Soil moisture:** 62% is **adequate** for grass \u2014 no immediate irrigation needed.  \n- **Weather:** Warm and moderately dry (31\u00b0C, 43% humidity) \u2014 typical for early afternoon.\n\n### \ud83d\udd04 **Recent Discussion Context**\nYou\u2019ve shown interest in **switching from grass to sugarcane**.  \n- We discussed fertilizer and irrigation plans for sugarcane.  \n- Your current moisture readings are stable, but if you proceed with sugarcane, you\u2019ll need to **adjust irrigation rules** and **prepare the soil**.\n\n### \ud83d\ude9c **Suggested Immediate Actions**\n1. Verify moisture sensor placement (readings earlier showed 0% \u2014 now 62%).\n2. If switching to sugarcane, start **land preparation** and **soil testing**.\n3. Visit the [Auto-Irrigation](/auto-irrigation) page when ready to set sugarcane-specific rules.\n\nWould you like to proceed with sugarcane planning, or address something else on your farm?"}, {"sender": "user", "text": "which seeds should I prefer for wheets"}, {"sender": "bot", "text": "For wheat cultivation on your red soil (likely in Maharashtra), here are **recommended wheat varieties**:\n\n### \ud83c\udf3e **High-Yielding & Disease-Resistant Varieties for Maharashtra**\n| Variety | Key Features | Suitable For |\n| :--- | :--- | :--- |\n| **HD-3086** | High yield, resistant to rust & blight, medium duration (110-115 days) | Timely sown irrigated conditions |\n| **HD-2967** | High protein, resistant to brown rust, suitable for timely sowing | Irrigated plains of Peninsular India |\n| **NIAW-1415** | Developed for drought-prone areas, good for limited irrigation | Rainfed or scarce water regions |\n| **HI-1544** | Heat-tolerant, good for late sowing | Late planting situations |\n\n### \ud83d\udccc **Immediate Steps**\n1. **Confirm sowing time:** In Maharashtra, **timely sowing** (Nov \u2013 early Dec) is crucial for high yields.\n2. **Seed treatment:** Use fungicides like **Carbendazim** (2g/kg seed) to prevent soil-borne diseases.\n3. **Soil preparation:** Red soil needs good tilth \u2013 add 5\u201310 tonnes/acre of compost/FYM before planting.\n4. **Irrigation:** Wheat needs 4\u20136 irrigations; adjust rules on [Auto-Irrigation](/auto-irrigation) for moisture ~70-80% during vegetative growth.\n\n### \ud83d\udca1 **Pro Tip**\nFor red soil, consider **HD-3086** if you have assured irrigation, or **NIAW-1415** if water is limited. Always use **certified seeds** from reliable sources.\n\nWould you like help with wheat-specific fertilizer schedules or irrigation settings?"}], "title": "hello there, how is it going?"}');
INSERT INTO "espDetails" ("macADD","userID","lastActive","battery") VALUES ('3C:8A:1F:76:FB:C4',1,'2026-04-09 17:19:47',6);
INSERT INTO "esp_logs" ("entry_id","espID","temperature","humidity","relay","moisture","water_level","entry_timestamp") VALUES (12105,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:15'),
 (12106,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:17'),
 (12107,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:18:22'),
 (12108,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:23'),
 (12109,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,156,'2026-04-09 17:18:25'),
 (12110,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:27'),
 (12111,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:28'),
 (12112,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:18:33'),
 (12113,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:37'),
 (12114,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:18:38'),
 (12115,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:40'),
 (12116,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,146,'2026-04-09 17:18:44'),
 (12117,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,153,'2026-04-09 17:18:45'),
 (12118,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:18:47'),
 (12119,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:48'),
 (12120,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,156,'2026-04-09 17:18:50'),
 (12121,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,153,'2026-04-09 17:18:53'),
 (12122,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:54'),
 (12123,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,156,'2026-04-09 17:18:55'),
 (12124,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:57'),
 (12125,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:18:59'),
 (12126,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,156,'2026-04-09 17:19:01'),
 (12127,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:19:03'),
 (12128,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,153,'2026-04-09 17:19:04'),
 (12129,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:06'),
 (12130,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:19:22'),
 (12131,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:25'),
 (12132,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,156,'2026-04-09 17:19:26'),
 (12133,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,153,'2026-04-09 17:19:27'),
 (12134,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:19:28'),
 (12135,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:32'),
 (12136,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,140,'2026-04-09 17:19:35'),
 (12137,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:36'),
 (12138,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,143,'2026-04-09 17:19:38'),
 (12139,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:19:39'),
 (12140,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:41'),
 (12141,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:19:43'),
 (12142,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:44');
INSERT INTO "farmDetails" ("id","userID","espID","location","totalArea","soilType","cropType") VALUES (1,1,'3C:8A:1F:76:FB:C4','18.442001,73.8304973',1,'Red Soil','Grass');
INSERT INTO "irrigation_rules" ("id","espID","ActiveStart","ActiveEnd","Rule") VALUES (33,'3C:8A:1F:76:FB:C4','2026-04-04 10:40:00','2026-04-04 10:41:00','{"rule_name": "[WH]OneMinToday", "rule_type": "periodic", "days": [6], "start_time": "104000", "end_time": "104100"}'),
 (34,'3C:8A:1F:76:FB:C4','2026-04-04 10:44:34','2026-04-04 10:45:34','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [6], "start_time": "104434", "end_time": "104534"}'),
 (35,'3C:8A:1F:76:FB:C4','2026-04-04 10:47:48','2026-04-04 10:48:18','{"rule_name": "[WH]now_30sec", "rule_type": "periodic", "days": [6], "start_time": "104748", "end_time": "104818"}'),
 (36,'3C:8A:1F:76:FB:C4','2026-04-04 10:49:00','2026-04-04 10:51:00','{"rule_name": "[WH]now2min", "rule_type": "periodic", "days": [6], "start_time": "104900", "end_time": "105100"}'),
 (50,'3C:8A:1F:76:FB:C4','2026-04-08 12:04:03','2026-04-08 12:04:08','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [3], "start_time": "120403", "end_time": "120408"}'),
 (51,'3C:8A:1F:76:FB:C4','2026-04-08 12:02:14','2026-04-08 12:02:19','{"rule_name": "[WH]irrigate_now", "rule_type": "periodic", "days": [3], "start_time": "120214", "end_time": "120219"}'),
 (52,'3C:8A:1F:76:FB:C4','2026-04-08 12:24:22','2026-04-08 12:24:27','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [3], "start_time": "122422", "end_time": "122427"}'),
 (65,'3C:8A:1F:76:FB:C4','2026-04-09 15:51:38','2026-04-09 15:52:08','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [4], "start_time": "155138", "end_time": "155208"}'),
 (66,'3C:8A:1F:76:FB:C4','2026-04-09 15:54:16','2026-04-09 15:54:41','{"rule_name": "[WH]immediate", "rule_type": "periodic", "days": [4], "start_time": "155416", "end_time": "155441"}'),
 (67,'3C:8A:1F:76:FB:C4','2026-04-09 15:56:08','2026-04-09 15:56:38','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [4], "start_time": "155608", "end_time": "155638"}'),
 (68,'3C:8A:1F:76:FB:C4','2026-04-09 16:06:51','2026-04-09 16:07:06','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [4], "start_time": "160651", "end_time": "160706"}'),
 (69,'3C:8A:1F:76:FB:C4','2026-04-09 16:20:40','2026-04-09 16:21:10','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [4], "start_time": "162040", "end_time": "162110"}');
INSERT INTO "notifications" ("id","userID","title","description","category","dateTime","read") VALUES (1,1,'Low Soil Moisture','The soil moisture level is below the threshold in Field A.','warn','2026-01-16 10:41:00',1),
 (2,1,'New Weather Alert','A new weather alert has been issued for your area.','info','2026-01-16 10:36:00',1),
 (3,1,'Irrigation Completed','The irrigation cycle for Field B has been successfully completed.','success','2026-01-16 16:31:00',1),
 (4,1,'Pest Detected','Pests have been detected in Field C. Please take necessary actions.','danger','2026-01-16 16:26:00',1),
 (5,1,'Nutrient Deficiency','Nutrient deficiency detected in Crop D. Consider fertilization.','warn','2026-01-16 16:21:00',1);
INSERT INTO "userDetails" ("id","userName","emailId","oAuth","password","espID","profilePic") VALUES (1,'TestUser','test@test.test','','12345678','3C:8A:1F:76:FB:C4',NULL);
CREATE INDEX IF NOT EXISTS "ESPIDs" ON "esp_logs" (
	"espID"	ASC
);
COMMIT;

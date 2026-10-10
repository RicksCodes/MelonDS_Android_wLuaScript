-- platinum-item-and-nature.lua
-- Pokémon Platino: mostra solo la natura e lo strumento tenuto del Pokémon selvatico in lotta.
-- Funziona su melonDS (versione Lua) e DeSmuME, con tutte le lingue di Platino.
-- Nelle lotte in doppio i tasti 3 / 4 passano dal primo al secondo Pokémon avversario.

---------------------------------------------------------------------------------------------------
-- Personalizzazione
---------------------------------------------------------------------------------------------------
local LINGUA_NATURA = "it"          -- "it" = nomi italiani (Modesta, Decisa...), "en" = inglesi (Modest, Adamant...)
local MOSTRA_ETICHETTE = true       -- true = "Natura: Modesta", false = solo "Modesta"
local POSIZIONE = "alto-sinistra"   -- "alto-sinistra", "alto-destra", "basso-sinistra", "basso-destra" (schermo superiore)
local COLORE_TESTO = "#FFFFFF"
local COLORE_NATURA = "#FFE070"     -- colore del nome della natura
local COLORE_CONTORNO = "#000000"
local COLORE_SFONDO = "#000000A0"   -- ultime due cifre = trasparenza (00 invisibile, FF pieno)
---------------------------------------------------------------------------------------------------

local read32 = memory.readdwordunsigned
local read16 = memory.readword
local rshift, band, bxor = bit.rshift, bit.band, bit.bxor

local natureNamesEn = {
 "Hardy", "Lonely", "Brave", "Adamant", "Naughty",
 "Bold", "Docile", "Relaxed", "Impish", "Lax",
 "Timid", "Hasty", "Serious", "Jolly", "Naive",
 "Modest", "Mild", "Quiet", "Bashful", "Rash",
 "Calm", "Gentle", "Sassy", "Careful", "Quirky"}

local natureNamesIt = {
 "Ardita", "Schiva", "Audace", "Decisa", "Birbona",
 "Sicura", "Docile", "Placida", "Scaltra", "Fiacca",
 "Timida", "Lesta", "Seria", "Allegra", "Ingenua",
 "Modesta", "Mite", "Quieta", "Ritrosa", "Ardente",
 "Calma", "Gentile", "Vivace", "Cauta", "Furba"}

local natureNames = LINGUA_NATURA == "en" and natureNamesEn or natureNamesIt

-- Nomi degli strumenti (in inglese, come negli strumenti RNG)
local itemNames = {
 "None", "Master Ball", "Ultra Ball", "Great Ball", "Poké Ball", "Safari Ball", "Net Ball", "Dive Ball",
 "Nest Ball", "Repeat Ball", "Timer Ball", "Luxury Ball", "Premier Ball", "Dusk Ball", "Heal Ball",
 "Quick Ball", "Cherish Ball", "Potion", "Antidote", "Burn Heal", "Ice Heal", "Awakening", "Parlyz Heal",
 "Full Restore", "Max Potion", "Hyper Potion", "Super Potion", "Full Heal", "Revive", "Max Revive",
 "Fresh Water", "Soda Pop", "Lemonade", "Moomoo Milk", "EnergyPowder", "Energy Root", "Heal Powder",
 "Revival Herb", "Ether", "Max Ether", "Elixir", "Max Elixir", "Lava Cookie", "Berry Juice",
 "Sacred Ash", "HP Up", "Protein", "Iron", "Carbos", "Calcium", "Rare Candy", "PP Up", "Zinc",
 "PP Max", "Old Gateau", "Guard Spec.", "Dire Hit", "X Attack", "X Defend", "X Speed", "X Accuracy",
 "X Special", "X Sp. Def", "Poké Doll", "Fluffy Tail", "Blue Flute", "Yellow Flute", "Red Flute",
 "Black Flute", "White Flute", "Shoal Salt", "Shoal Shell", "Red Shard", "Blue Shard", "Yellow Shard",
 "Green Shard", "Super Repel", "Max Repel", "Escape Rope", "Repel", "Sun Stone", "Moon Stone",
 "Fire Stone", "Thunderstone", "Water Stone", "Leaf Stone", "TinyMushroom", "Big Mushroom", "Pearl",
 "Big Pearl", "Stardust", "Star Piece", "Nugget", "Heart Scale", "Honey", "Growth Mulch",
 "Damp Mulch", "Stable Mulch", "Gooey Mulch", "Root Fossil", "Claw Fossil", "Helix Fossil", "Dome Fossil",
 "Old Amber", "Armor Fossil", "Skull Fossil", "Rare Bone", "Shiny Stone", "Dusk Stone", "Dawn Stone",
 "Oval Stone", "Odd Keystone", "Griseous Orb", "unknown1", "unknown2", "unknown3", "unknown4", "unknown5",
 "unknown6", "unknown7", "unknown8", "unknown9", "unknown10", "unknown11", "unknown12", "unknown13",
 "unknown14", "unknown15", "unknown16", "unknown17", "unknown18", "unknown19", "unknown20", "unknown21",
 "unknown22", "Adamant Orb", "Lustrous Orb", "Grass Mail", "Flame Mail", "Bubble Mail", "Bloom Mail",
 "Tunnel Mail", "Steel Mail", "Heart Mail", "Snow Mail", "Space Mail", "Air Mail", "Mosaic Mail", "Brick Mail",
 "Cheri Berry", "Chesto Berry", "Pecha Berry", "Rawst Berry", "Aspear Berry", "Leppa Berry", "Oran Berry",
 "Persim Berry", "Lum Berry", "Sitrus Berry", "Figy Berry", "Wiki Berry", "Mago Berry", "Aguav Berry",
 "Iapapa Berry", "Razz Berry", "Bluk Berry", "Nanab Berry", "Wepear Berry", "Pinap Berry", "Pomeg Berry",
 "Kelpsy Berry", "Qualot Berry", "Hondew Berry", "Grepa Berry", "Tamato Berry", "Cornn Berry", "Magost Berry",
 "Rabuta Berry", "Nomel Berry", "Spelon Berry", "Pamtre Berry", "Watmel Berry", "Durin Berry",
 "Belue Berry", "Occa Berry", "Passho Berry", "Wacan Berry", "Rindo Berry", "Yache Berry",
 "Chople Berry", "Kebia Berry", "Shuca Berry", "Coba Berry", "Payapa Berry", "Tanga Berry",
 "Charti Berry", "Kasib Berry", "Haban Berry", "Colbur Berry", "Babiri Berry", "Chilan Berry", "Liechi Berry",
 "Ganlon Berry", "Salac Berry", "Petaya Berry", "Apicot Berry", "Lansat Berry", "Starf Berry",
 "Enigma Berry", "Micle Berry", "Custap Berry", "Jaboca Berry", "Rowap Berry", "BrightPowder",
 "White Herb", "Macho Brace", "Exp. Share", "Quick Claw", "Soothe Bell", "Mental Herb", "Choice Band",
 "King's Rock", "SilverPowder", "Amulet Coin", "Cleanse Tag", "Soul Dew", "DeepSeaTooth",
 "DeepSeaScale", "Smoke Ball", "Everstone", "Focus Band", "Lucky Egg", "Scope Lens", "Metal Coat",
 "Leftovers", "Dragon Scale", "Light Ball", "Soft Sand", "Hard Stone", "Miracle Seed", "BlackGlasses",
 "Black Belt", "Magnet", "Mystic Water", "Sharp Beak", "Poison Barb", "NeverMeltIce", "Spell Tag",
 "TwistedSpoon", "Charcoal", "Dragon Fang", "Silk Scarf", "Up-Grade", "Shell Bell", "Sea Incense",
 "Lax Incense", "Lucky Punch", "Metal Powder", "Thick Club", "Stick", "Red Scarf", "Blue Scarf",
 "Pink Scarf", "Green Scarf", "Yellow Scarf", "Wide Lens", "Muscle Band", "Wise Glasses", "Expert Belt",
 "Light Clay", "Life Orb", "Power Herb", "Toxic Orb", "Flame Orb", "Quick Powder", "Focus Sash",
 "Zoom Lens", "Metronome", "Iron Ball", "Lagging Tail", "Destiny Knot", "Black Sludge", "Icy Rock",
 "Smooth Rock", "Heat Rock", "Damp Rock", "Grip Claw", "Choice Scarf", "Sticky Barb", "Power Bracer",
 "Power Belt", "Power Lens", "Power Band", "Power Anklet", "Power Weight", "Shed Shell",
 "Big Root", "Choice Specs", "Flame Plate", "Splash Plate", "Zap Plate", "Meadow Plate", "Icicle Plate",
 "Fist Plate", "Toxic Plate", "Earth Plate", "Sky Plate", "Mind Plate", "Insect Plate",
 "Stone Plate", "Spooky Plate", "Draco Plate", "Dread Plate", "Iron Plate", "Odd Incense",
 "Rock Incense", "Full Incense", "Wave Incense", "Rose Incense", "Luck Incense", "Pure Incense",
 "Protector", "Electirizer", "Magmarizer", "Dubious Disc", "Reaper Cloth", "Razor Claw", "Razor Fang",
 "TM01", "TM02", "TM03", "TM04", "TM05", "TM06", "TM07", "TM08", "TM09", "TM10", "TM11", "TM12",
 "TM13", "TM14", "TM15", "TM16", "TM17", "TM18", "TM19", "TM20", "TM21", "TM22", "TM23", "TM24",
 "TM25", "TM26", "TM27", "TM28", "TM29", "TM30", "TM31", "TM32", "TM33", "TM34", "TM35", "TM36",
 "TM37", "TM38", "TM39", "TM40", "TM41", "TM42", "TM43", "TM44", "TM45", "TM46", "TM47", "TM48",
 "TM49", "TM50", "TM51", "TM52", "TM53", "TM54", "TM55", "TM56", "TM57", "TM58", "TM59", "TM60",
 "TM61", "TM62", "TM63", "TM64", "TM65", "TM66", "TM67", "TM68", "TM69", "TM70", "TM71", "TM72",
 "TM73", "TM74", "TM75", "TM76", "TM77", "TM78", "TM79", "TM80", "TM81", "TM82", "TM83", "TM84",
 "TM85", "TM86", "TM87", "TM88", "TM89", "TM90", "TM91", "TM92", "HM01", "HM02", "HM03", "HM04",
 "HM05", "HM06", "HM07", "HM08", "Explorer Kit", "Loot Sack", "Rule Book", "Poké Radar", "Point Card",
 "Journal", "Seal Case", "Fashion Case", "Seal Bag", "Pal Pad", "Works Key", "Old Charm",
 "Galactic Key", "Red Chain", "Town Map", "Vs. Seeker", "Coin Case", "Old Rod", "Good Rod", "Super Rod",
 "Sprayduck", "Poffin Case", "Bicycle", "Suite Key", "Oak's Letter", "Lunar Wing", "Member Card",
 "Azure Flute", "S.S. Ticket", "Contest Pass", "Magma Stone", "Parcel", "Coupon 1", "Coupon 2",
 "Coupon 3", "Storage Key", "SecretPotion", "Vs. Recorder", "Gracidea", "Secret Key",
 "Apricorn Box", "Unown Report", "Berry Pots", "Dowsing MCHN", "Blue Card", "Slowpoke Tail",
 "Clear Bell", "Card Key", "Basement Key", "SquirtBottle", "Red Scale", "Lost Item", "Pass",
 "Machine Part", "Silver Wing", "Rainbow Wing", "Mystery Egg", "Red Apricorn", "Yellow Apricorn",
 "Blue Apricorn", "Green Apricorn", "Pink Apricorn", "White Apricorn", "Black Apricorn", "Fast Ball",
 "Level Ball", "Lure Ball", "Heavy Ball", "Love Ball", "Friend Ball", "Moon Ball",
 "Sport Ball", "Park Ball", "Photo Album", "GB Sounds", "Tidal Bell", "RageCandyBar", "Data Card 01",
 "Data Card 02", "Data Card 03", "Data Card 04", "Data Card 05", "Data Card 06", "Data Card 07",
 "Data Card 08", "Data Card 09", "Data Card 10", "Data Card 11", "Data Card 12", "Data Card 13",
 "Data Card 14", "Data Card 15", "Data Card 16", "Data Card 17", "Data Card 18", "Data Card 19",
 "Data Card 20", "Data Card 21", "Data Card 22", "Data Card 23", "Data Card 24", "Data Card 25",
 "Data Card 26", "Data Card 27", "Jade Orb", "Lock Capsule", "Red Orb", "Blue Orb", "Enigma Stone"}

local langTable = {
 [0x44] = {0x2101ECC, 0},     -- GER
 [0x45] = {0x2101D2C, 0},     -- EUR/USA
 [0x46] = {0x2101F0C, 0},     -- FRE
 [0x49] = {0x2101E8C, 0},     -- ITA
 [0x4A] = {0x210112C, 0},     -- JPN
 [0x4B] = {0x2102C2C, 0x44},  -- KOR
 [0x53] = {0x2101F2C, 0}}     -- SPA

-- Posizione del blocco "Growth" (specie, strumento) per ognuno dei 24 ordini dei blocchi
local GROWTH_POS = {0,0,0,0,0,0, 1,1,2,3,2,3, 1,1,2,3,2,3, 1,1,2,3,2,3}

local gameCode = read32(0x2FFFE0C)
local isPlatinum = band(gameCode, 0xFFFFFF) == 0x555043
local lang = langTable[rshift(gameCode, 24)]

if not isPlatinum or not lang then
 print("Gioco non supportato: serve Pokémon Platino (GER/EUR-USA/FRE/ITA/JPN/KOR/SPA).")
 return
end

local pidPointerAddr, koreanOffset = lang[1], lang[2]

local function LCRNG(s, mul, sum)
 local a = rshift(mul, 16) * (s % 0x10000) + rshift(s, 16) * (mul % 0x10000)
 local b = (mul % 0x10000) * (s % 0x10000) + (a % 0x10000) * 0x10000 + sum
 return b % 0x100000000
end

-- Decifra PID, specie e strumento tenuto
local function readWild(addr)
 local pid = read32(addr)
 local checksum = read16(addr + 0x6)
 local order = (rshift(band(pid, 0x3E000), 0xD) % 24) + 1
 local blocks = GROWTH_POS[order]
 local prng = checksum

 for i = 1, blocks do
  prng = LCRNG(prng, 0x5F748241, 0xCBA72510)  -- salta 16 cicli per blocco
 end

 local base = addr + blocks * 32
 prng = LCRNG(prng, 0x41C64E6D, 0x6073)
 local species = bxor(read16(base + 0x8), rshift(prng, 16))
 prng = LCRNG(prng, 0x41C64E6D, 0x6073)
 local item = bxor(read16(base + 0xA), rshift(prng, 16))

 return pid, species, item
end

local slot, prevKey = 0, {}

-- Dimensioni del testo (font 5x7 di DeSmuME/melonDS: 6 pixel per carattere, righe da 10)
local CHAR_W, LINE_H, PAD = 6, 10, 3

local function textWidth(s)
 -- In UTF-8 un carattere accentato occupa più byte: i byte di continuazione (0x80-0xBF) non sono colonne
 local _, continuation = s:gsub("[\128-\191]", "")
 return (#s - continuation) * CHAR_W
end

local function main()
 local key = input.get()
 local pressed3 = (key["3"] or key["numpad3"]) and not (prevKey["3"] or prevKey["numpad3"])
 local pressed4 = (key["4"] or key["numpad4"]) and not (prevKey["4"] or prevKey["numpad4"])
 if pressed3 or pressed4 then slot = 1 - slot end
 prevKey = key

 local enemyAddr = read32(pidPointerAddr) + 0x58E3C + koreanOffset + 0xEC * slot
 local pid, species, item = readWild(enemyAddr)

 -- Fuori dalla lotta (o dati non validi): schermo pulito
 if pid == 0 or species < 1 or species > 493 then return end

 local labelNature = MOSTRA_ETICHETTE and "Natura: " or ""
 local labelItem = MOSTRA_ETICHETTE and "Strumento: " or ""
 local nature = natureNames[(pid % 25) + 1]
 local itemName = item == 0 and "nessuno" or (itemNames[item + 1] or "???")

 local width = math.max(textWidth(labelNature .. nature), textWidth(labelItem .. itemName)) + PAD * 2
 local height = LINE_H * 2 + PAD * 2 - 2

 -- Schermo superiore: y da -192 a -1
 local x = POSIZIONE:find("destra") and (256 - width - 2) or 2
 local y = POSIZIONE:find("basso") and (-height - 2) or -190

 gui.box(x, y, x + width, y + height, COLORE_SFONDO, COLORE_SFONDO)
 gui.text(x + PAD, y + PAD, labelNature, COLORE_TESTO, COLORE_CONTORNO)
 gui.text(x + PAD + textWidth(labelNature), y + PAD, nature, COLORE_NATURA, COLORE_CONTORNO)
 gui.text(x + PAD, y + PAD + LINE_H, labelItem .. itemName, COLORE_TESTO, COLORE_CONTORNO)
end

gui.register(main)

-- Pt_Autoplay.lua
-- Pokémon Platino: gioca da solo finché non trova il Pokémon che cerchi.
--
--   * "selvatici": gira su sé stesso nell'erba (o in grotta) senza cambiare casella, oppure cammina avanti e
--                  indietro; controlla ogni Pokémon selvatico e fugge se non è quello giusto.
--   * "fisso":     Pokémon fisso/leggendario (Giratina, Dialga, Palkia, Heatran, Rotom...). Salva DAVANTI al
--                  Pokémon, poi lo script fa soft reset, preme A fino alla lotta, controlla e ricomincia.
--   * "regalo":    Pokémon regalato (fossili, Eevee, Porygon, uovo di Togepi...). Salva DAVANTI a chi te lo dà,
--                  poi soft reset + A finché non entra in squadra, controlla e ricomincia.
--
-- Si ferma quando un Pokémon corrisponde a UNO dei profili in BERSAGLI (dentro un profilo valgono TUTTE le
-- condizioni). Puoi cercare specie, strumento tenuto, shiny, natura, IV e abilità.
--
-- Richiede MelonDS Android con Lua con joypad.set / stylus.set funzionanti (patch "lua-joypad-set").
-- Funziona anche su DeSmuME. Lingue: ITA, ENG, FRA, GER, SPA, JPN (KOR non verificata).
--
-- Tasti dello script (tastierino 1–0 di MelonDS, o tastiera):
--   1 = pausa / riprendi (in pausa giochi tu)     2 = mostra / nascondi il pannello
--   3 = diagnostica (indicatore di lotta, puntatore giocatore, numero di avversari letti)
--   4 = quando si è fermato (trovato o errore) riparte con la ricerca
--
-- Consiglio: usa l'avanti veloce dell'emulatore, lo script segue la velocità del gioco.

---------------------------------------------------------------------------------------------------
-- CONFIGURAZIONE
---------------------------------------------------------------------------------------------------

local MODALITA = "selvatici"     -- "selvatici", "fisso" oppure "regalo"

local FERMA_SE_SHINY = true      -- si ferma su QUALSIASI Pokémon shiny, anche se non è nei bersagli qui sotto

-- Il Pokémon cercato. Lo script si ferma al primo che soddisfa TUTTE le condizioni di almeno UN profilo.
-- Lascia nil (o togli la riga) per "qualsiasi".
--   specie    = nome inglese/italiano (uguali quasi sempre) o numero del Pokédex nazionale. Anche una lista.
--   strumento = nome INGLESE dello strumento (es. "Leftovers", "TwistedSpoon", "Star Piece") o numero.
--               "qualsiasi" = basta che tenga uno strumento qualsiasi. Anche una lista.
--   shiny     = true per pretenderlo cromatico
--   natura    = nome italiano o inglese ("Modesta" / "Modest"). Anche una lista: {"Timida", "Modesta"}
--   abilita   = nome inglese dell'abilità (es. "Synchronize"). Anche una lista.
--   iv_min    = IV minimi:  { ps = 31, att = 0, dif = 0, attsp = 31, difsp = 0, vel = 31 } (metti solo quelli che ti servono)
--   iv_max    = IV massimi: es. { vel = 0 } per Trick Room
local BERSAGLI = {
 -- Chansey con Fortunuovo (Lucky Egg)
 { specie = "Chansey", strumento = "Lucky Egg" },

 -- Altri esempi (togli i "--" davanti per usarli):
 -- { specie = "Abra", strumento = "TwistedSpoon", natura = {"Timida", "Modesta"} },
 -- { specie = "Gible", natura = "Allegra", iv_min = { att = 25, vel = 25 } },
 -- { specie = "Giratina", iv_min = { ps = 28, attsp = 28, vel = 28 } },
 -- { strumento = "qualsiasi" },
}

-- Tutti i Pokémon selvatici di Platino che possono tenere uno strumento, bacche escluse (dati del gioco, dal decompilato
-- pret/pokeplatinum). Accanto: nome italiano, probabilità che lo tenga, dove si trova.
-- Lo script incontra solo i Pokémon di erba e grotte (anche sciami, Poké Radar e Giardino Trofeo se attivi).
-- Pesca, alberi di miele e cartuccia GBA restano in lista ma lo script non li fa comparire; nella Gran Palude
-- (zona Safari) la fuga non funziona. Metti "--" davanti a una riga per escluderla.
local CERCA_STRUMENTI_SELVATICI = true   -- false = ignora tutta la lista qui sotto
local STRUMENTI_SELVATICI = {
 { specie = "Fearow", strumento = "Sharp Beak" },           -- Beccaffilato 5% - erba/grotte
 { specie = "Pikachu", strumento = "Light Ball" },          -- Elettropalla 5% - Giardino Trofeo
 { specie = "Sandshrew", strumento = "Quick Claw" },        -- Rapidartigli 5% - cartuccia GBA
 { specie = "Sandslash", strumento = "Quick Claw" },        -- Rapidartigli 5% - cartuccia GBA
 { specie = "Clefairy", strumento = "Moon Stone" },         -- Pietralunare 5% - erba/grotte, Giardino Trofeo
 { specie = "Paras", strumento = "Big Mushroom" },          -- Grande Fungo 5% - Gran Palude
 { specie = "Paras", strumento = "TinyMushroom" },          -- Minifungo 50% - Gran Palude
 { specie = "Venomoth", strumento = "Shed Shell" },         -- Disfoguscio 5% - Poké Radar
 { specie = "Diglett", strumento = "Soft Sand" },           -- Sabbia Soffice 5% - erba/grotte, Poké Radar, cartuccia GBA
 { specie = "Dugtrio", strumento = "Soft Sand" },           -- Sabbia Soffice 5% - erba/grotte
 { specie = "Meowth", strumento = "Quick Claw" },           -- Rapidartigli 5% - Giardino Trofeo
 { specie = "Poliwhirl", strumento = "King's Rock" },       -- Roccia di Re 5% - surf
 { specie = "Abra", strumento = "TwistedSpoon" },           -- Cucchiaio Torto 5% - erba/grotte, Poké Radar
 { specie = "Kadabra", strumento = "TwistedSpoon" },        -- Cucchiaio Torto 5% - erba/grotte, Poké Radar, cartuccia GBA
 { specie = "Tentacool", strumento = "Poison Barb" },       -- Velenaculeo 5% - surf
 { specie = "Tentacruel", strumento = "Poison Barb" },      -- Velenaculeo 5% - surf
 { specie = "Geodude", strumento = "Everstone" },           -- Pietrastante 5% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Graveler", strumento = "Everstone" },          -- Pietrastante 5% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Slowpoke", strumento = "Lagging Tail" },       -- Rallentocoda 5% - Poké Radar
 { specie = "Magnemite", strumento = "Metal Coat" },        -- Metalcoperta 5% - erba/grotte, sciame
 { specie = "Magneton", strumento = "Metal Coat" },         -- Metalcoperta 5% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Farfetch'd", strumento = "Stick" },            -- Gambo 5% - sciame
 { specie = "Doduo", strumento = "Sharp Beak" },            -- Beccaffilato 5% - sciame
 { specie = "Grimer", strumento = "Nugget" },               -- Pepita 5% - Poké Radar
 { specie = "Shellder", strumento = "Big Pearl" },          -- Grande Perla 5% - pesca
 { specie = "Shellder", strumento = "Pearl" },              -- Perla 50% - pesca
 { specie = "Cubone", strumento = "Thick Club" },           -- Ossospesso 5% - sciame
 { specie = "Lickitung", strumento = "Lagging Tail" },      -- Rallentocoda 5% - erba/grotte, Poké Radar
 { specie = "Koffing", strumento = "Smoke Ball" },          -- Palla Fumo 5% - erba/grotte, Poké Radar, cartuccia GBA
 { specie = "Weezing", strumento = "Smoke Ball" },          -- Palla Fumo 5% - erba/grotte, Poké Radar
 { specie = "Chansey", strumento = "Lucky Egg" },           -- Fortunuovo 5% - erba/grotte, Giardino Trofeo, cartuccia GBA
 { specie = "Chansey", strumento = "Oval Stone" },          -- Pietraovale 50% - erba/grotte, Giardino Trofeo, cartuccia GBA
 { specie = "Horsea", strumento = "Dragon Scale" },         -- Squama Drago 5% - pesca
 { specie = "Seadra", strumento = "Dragon Scale" },         -- Squama Drago 5% - pesca
 { specie = "Staryu", strumento = "Star Piece" },           -- Pezzo Stella 5% - pesca
 { specie = "Staryu", strumento = "Stardust" },             -- Polvostella 50% - pesca
 { specie = "Electabuzz", strumento = "Electirizer" },      -- Elettritore 5% - erba/grotte
 { specie = "Magmar", strumento = "Magmarizer" },           -- Magmatore 5% - erba/grotte, sciame
 { specie = "Ditto", strumento = "Metal Powder" },          -- Metalpolvere 5% - Giardino Trofeo
 { specie = "Ditto", strumento = "Quick Powder" },          -- Velopolvere 50% - Giardino Trofeo
 { specie = "Dratini", strumento = "Dragon Scale" },        -- Squama Drago 5% - pesca
 { specie = "Dragonair", strumento = "Dragon Scale" },      -- Squama Drago 5% - pesca
 { specie = "Chinchou", strumento = "DeepSeaScale" },       -- Squamabissi 5% - pesca
 { specie = "Cleffa", strumento = "Moon Stone" },           -- Pietralunare 5% - erba/grotte, Giardino Trofeo
 { specie = "Yanma", strumento = "Wide Lens" },             -- Grandelente 5% - Poké Radar, Gran Palude, cartuccia GBA
 { specie = "Steelix", strumento = "Metal Coat" },          -- Metalcoperta 5% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Qwilfish", strumento = "Poison Barb" },        -- Velenaculeo 5% - pesca
 { specie = "Sneasel", strumento = "Grip Claw" },           -- Presartigli 50% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Sneasel", strumento = "Quick Claw" },          -- Rapidartigli 5% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Corsola", strumento = "Hard Stone" },          -- Pietradura 5% - sciame
 { specie = "Miltank", strumento = "Moomoo Milk" },         -- Latte Mumu 100% - Poké Radar
 { specie = "Beautifly", strumento = "Shed Shell" },        -- Disfoguscio 5% - erba/grotte, Poké Radar, cartuccia GBA
 { specie = "Dustox", strumento = "Shed Shell" },           -- Disfoguscio 5% - erba/grotte, Poké Radar, cartuccia GBA
 { specie = "Masquerain", strumento = "SilverPowder" },     -- Argenpolvere 5% - surf
 { specie = "Nosepass", strumento = "Hard Stone" },         -- Pietradura 5% - erba/grotte
 { specie = "Aron", strumento = "Hard Stone" },             -- Pietradura 5% - Poké Radar
 { specie = "Roselia", strumento = "Poison Barb" },         -- Velenaculeo 5% - erba/grotte, sciame, Giardino Trofeo, cartuccia GBA
 { specie = "Gulpin", strumento = "Big Pearl" },            -- Grande Perla 5% - Gran Palude
 { specie = "Carvanha", strumento = "DeepSeaTooth" },       -- Dente Abissi 5% - pesca
 { specie = "Cacnea", strumento = "Sticky Barb" },          -- Vischiopunta 5% - erba/grotte, Poké Radar, cartuccia GBA
 { specie = "Cacturne", strumento = "Sticky Barb" },        -- Vischiopunta 5% - erba/grotte
 { specie = "Zangoose", strumento = "Quick Claw" },         -- Rapidartigli 5% - cartuccia GBA
 { specie = "Lunatone", strumento = "Moon Stone" },         -- Pietralunare 5% - cartuccia GBA
 { specie = "Solrock", strumento = "Sun Stone" },           -- Pietrasolare 5% - cartuccia GBA
 { specie = "Castform", strumento = "Mystic Water" },       -- Acqua Magica 100% - Giardino Trofeo
 { specie = "Banette", strumento = "Spell Tag" },           -- Spettrotarga 5% - erba/grotte
 { specie = "Relicanth", strumento = "DeepSeaScale" },      -- Squamabissi 5% - pesca
 { specie = "Luvdisc", strumento = "Heart Scale" },         -- Squama Cuore 50% - pesca
 { specie = "Bagon", strumento = "Dragon Fang" },           -- Dentedidrago 5% - Poké Radar
 { specie = "Beldum", strumento = "Metal Coat" },           -- Metalcoperta 5% - sciame
 { specie = "Kricketot", strumento = "Metronome" },         -- Plessimetro 5% - erba/grotte
 { specie = "Kricketune", strumento = "Metronome" },        -- Plessimetro 5% - erba/grotte, Giardino Trofeo
 { specie = "Budew", strumento = "Poison Barb" },           -- Velenaculeo 5% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Combee", strumento = "Honey" },                -- Miele 100% - albero di miele
 { specie = "Cherubi", strumento = "Miracle Seed" },        -- Miracolseme 5% - albero di miele
 { specie = "Bronzor", strumento = "Metal Coat" },          -- Metalcoperta 5% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Bronzong", strumento = "Metal Coat" },         -- Metalcoperta 5% - erba/grotte, sciame
 { specie = "Happiny", strumento = "Lucky Punch" },         -- Fortunpugno 5% - Giardino Trofeo
 { specie = "Happiny", strumento = "Oval Stone" },          -- Pietraovale 50% - Giardino Trofeo
 { specie = "Chatot", strumento = "Metronome" },            -- Plessimetro 5% - erba/grotte
 { specie = "Munchlax", strumento = "Leftovers" },          -- Avanzi 100% - albero di miele
 { specie = "Skorupi", strumento = "Poison Barb" },         -- Velenaculeo 5% - Gran Palude
 { specie = "Drapion", strumento = "Poison Barb" },         -- Velenaculeo 5% - Gran Palude
 { specie = "Croagunk", strumento = "Black Sludge" },       -- Fangopece 5% - erba/grotte, Gran Palude
 { specie = "Toxicroak", strumento = "Black Sludge" },      -- Fangopece 5% - Gran Palude
 { specie = "Snover", strumento = "NeverMeltIce" },         -- Gelomai 5% - erba/grotte, sciame, Poké Radar, cartuccia GBA
 { specie = "Abomasnow", strumento = "NeverMeltIce" },      -- Gelomai 5% - erba/grotte, sciame
}

-- Modalità "selvatici"
local MOVIMENTO = "gira"         -- "gira" = gira su sé stesso nella stessa casella (gli incontri partono lo stesso)
                                 -- "cammina" = avanti e indietro
-- Solo con MOVIMENTO = "cammina":
local DIREZIONE = "orizzontale"  -- "orizzontale" (sinistra/destra) o "verticale" (su/giù)
local FRAME_PER_PASSO = 8        -- frame di pressione per ogni direzione. Se il personaggio gira solo su sé stesso, alza (16)
local CORRI = true               -- tiene premuto B (scarpe da corsa)
local FUGA_X, FUGA_Y = 125, 175  -- punto del touch screen dove c'è "FUGA" nel menu di lotta
-- Il primo Pokémon della squadra deve poter scappare sempre: più veloce dei selvatici, oppure con Palla Fumo
-- o l'abilità Fuga. Se non riesce a fuggire per troppo tempo lo script si mette in pausa.

-- Modalità "fisso" e "regalo"
local RESET_CON = "emu"          -- "emu" = emu.reset() dell'emulatore, "tasti" = L+R+Start+Select del gioco
local DIREZIONE_FISSO = nil      -- nil, oppure "up"/"down"/"left"/"right" da tenere premuta mentre preme A
                                 -- (alcuni eventi richiedono di fare un passo verso il Pokémon)
local ATTESA_MAX = 60 * 90       -- frame massimi per arrivare alla lotta / al regalo dopo un reset, poi riprova

-- Varie
local MOSTRA_PANNELLO = true
local SALVA_REGISTRO = true      -- scrive ogni incontro in "incontri.csv" (cartella dello script)

---------------------------------------------------------------------------------------------------
-- Fine configurazione
---------------------------------------------------------------------------------------------------

local read8 = memory.readbyte
local read16 = memory.readword
local read32 = memory.readdwordunsigned
local band, bxor = bit.band, bit.bxor
local floor = math.floor

-- Nomi (dal tuo Pt_RNG_DeSmuMe.lua)
local speciesNamesList = {
 -- Gen 1
 "Bulbasaur", "Ivysaur", "Venusaur", "Charmander", "Charmeleon", "Charizard", "Squirtle", "Wartortle", "Blastoise",
 "Caterpie", "Metapod", "Butterfree", "Weedle", "Kakuna", "Beedrill", "Pidgey", "Pidgeotto", "Pidgeot", "Rattata",
 "Raticate", "Spearow", "Fearow", "Ekans", "Arbok", "Pikachu", "Raichu", "Sandshrew", "Sandslash", "Nidoran♀",
 "Nidorina", "Nidoqueen", "Nidoran♂", "Nidorino", "Nidoking", "Clefairy", "Clefable", "Vulpix", "Ninetales",
 "Jigglypuff", "Wigglytuff", "Zubat", "Golbat", "Oddish", "Gloom", "Vileplume", "Paras", "Parasect", "Venonat",
 "Venomoth", "Diglett", "Dugtrio", "Meowth", "Persian", "Psyduck", "Golduck", "Mankey", "Primeape", "Growlithe",
 "Arcanine", "Poliwag", "Poliwhirl", "Poliwrath", "Abra", "Kadabra", "Alakazam", "Machop", "Machoke", "Machamp",
 "Bellsprout", "Weepinbell", "Victreebel", "Tentacool", "Tentacruel", "Geodude", "Graveler", "Golem", "Ponyta",
 "Rapidash", "Slowpoke", "Slowbro", "Magnemite", "Magneton", "Farfetch'd", "Doduo", "Dodrio", "Seel", "Dewgong",
 "Grimer", "Muk", "Shellder", "Cloyster", "Gastly", "Haunter", "Gengar", "Onix", "Drowzee", "Hypno", "Krabby",
 "Kingler", "Voltorb", "Electrode", "Exeggcute", "Exeggutor", "Cubone", "Marowak", "Hitmonlee", "Hitmonchan",
 "Lickitung", "Koffing", "Weezing", "Rhyhorn", "Rhydon", "Chansey", "Tangela", "Kangaskhan", "Horsea", "Seadra",
 "Goldeen", "Seaking", "Staryu", "Starmie", "Mr. Mime", "Scyther", "Jynx", "Electabuzz", "Magmar", "Pinsir",
 "Tauros", "Magikarp", "Gyarados", "Lapras", "Ditto", "Eevee", "Vaporeon", "Jolteon", "Flareon", "Porygon",
 "Omanyte", "Omastar", "Kabuto", "Kabutops", "Aerodactyl", "Snorlax", "Articuno", "Zapdos", "Moltres", "Dratini",
 "Dragonair", "Dragonite", "Mewtwo", "Mew",
 -- Gen 2
 "Chikorita", "Bayleef", "Meganium", "Cyndaquil", "Quilava", "Typhlosion", "Totodile", "Croconaw", "Feraligatr",
 "Sentret", "Furret", "Hoothoot", "Noctowl", "Ledyba", "Ledian", "Spinarak", "Ariados", "Crobat", "Chinchou",
 "Lanturn", "Pichu", "Cleffa", "Igglybuff", "Togepi", "Togetic", "Natu", "Xatu", "Mareep", "Flaaffy", "Ampharos",
 "Bellossom", "Marill", "Azumarill", "Sudowoodo", "Politoed", "Hoppip", "Skiploom", "Jumpluff", "Aipom", "Sunkern",
 "Sunflora", "Yanma", "Wooper", "Quagsire", "Espeon", "Umbreon", "Murkrow", "Slowking", "Misdreavus", "Unown",
 "Wobbuffet", "Girafarig", "Pineco", "Forretress", "Dunsparce", "Gligar", "Steelix", "Snubbull", "Granbull",
 "Qwilfish", "Scizor", "Shuckle", "Heracross", "Sneasel", "Teddiursa", "Ursaring", "Slugma", "Magcargo", "Swinub",
 "Piloswine", "Corsola", "Remoraid", "Octillery", "Delibird", "Mantine", "Skarmory", "Houndour", "Houndoom",
 "Kingdra", "Phanpy", "Donphan", "Porygon2", "Stantler", "Smeargle", "Tyrogue", "Hitmontop", "Smoochum", "Elekid",
 "Magby", "Miltank", "Blissey", "Raikou", "Entei", "Suicune", "Larvitar", "Pupitar", "Tyranitar", "Lugia", "Ho-Oh",
 "Celebi",
 -- Gen 3
 "Treecko", "Grovyle", "Sceptile", "Torchic", "Combusken", "Blaziken", "Mudkip", "Marshtomp", "Swampert",
 "Poochyena", "Mightyena", "Zigzagoon", "Linoone", "Wurmple", "Silcoon", "Beautifly", "Cascoon", "Dustox", "Lotad",
 "Lombre", "Ludicolo", "Seedot", "Nuzleaf", "Shiftry", "Taillow", "Swellow", "Wingull", "Pelipper", "Ralts",
 "Kirlia", "Gardevoir", "Surskit", "Masquerain", "Shroomish", "Breloom", "Slakoth", "Vigoroth", "Slaking",
 "Nincada", "Ninjask", "Shedinja", "Whismur", "Loudred", "Exploud", "Makuhita", "Hariyama", "Azurill", "Nosepass",
 "Skitty", "Delcatty", "Sableye", "Mawile", "Aron", "Lairon", "Aggron", "Meditite", "Medicham", "Electrike",
 "Manectric", "Plusle", "Minun", "Volbeat", "Illumise", "Roselia", "Gulpin", "Swalot", "Carvanha", "Sharpedo",
 "Wailmer", "Wailord", "Numel", "Camerupt", "Torkoal", "Spoink", "Grumpig", "Spinda", "Trapinch", "Vibrava",
 "Flygon", "Cacnea", "Cacturne", "Swablu", "Altaria", "Zangoose", "Seviper", "Lunatone", "Solrock", "Barboach",
 "Whiscash", "Corphish", "Crawdaunt", "Baltoy", "Claydol", "Lileep", "Cradily", "Anorith", "Armaldo", "Feebas",
 "Milotic", "Castform", "Kecleon", "Shuppet", "Banette", "Duskull", "Dusclops", "Tropius", "Chimecho", "Absol",
 "Wynaut", "Snorunt", "Glalie", "Spheal", "Sealeo", "Walrein", "Clamperl", "Huntail", "Gorebyss", "Relicanth",
 "Luvdisc", "Bagon", "Shelgon", "Salamence", "Beldum", "Metang", "Metagross", "Regirock", "Regice", "Registeel",
 "Latias", "Latios",  "Kyogre", "Groudon", "Rayquaza", "Jirachi", "Deoxys",
 -- Gen 4
 "Turtwig", "Grotle", "Torterra", "Chimchar", "Monferno", "Infernape", "Piplup", "Prinplup", "Empoleon", "Starly",
 "Staravia", "Staraptor", "Bidoof", "Bibarel", "Kricketot", "Kricketune", "Shinx", "Luxio", "Luxray", "Budew",
 "Roserade", "Cranidos", "Rampardos", "Shieldon", "Bastiodon", "Burmy", "Wormadam", "Mothim", "Combee", "Vespiquen",
 "Pachirisu", "Buizel", "Floatzel", "Cherubi", "Cherrim", "Shellos", "Gastrodon", "Ambipom", "Drifloon", "Drifblim",
 "Buneary", "Lopunny", "Mismagius", "Honchkrow", "Glameow", "Purugly", "Chingling", "Stunky", "Skuntank", "Bronzor",
 "Bronzong", "Bonsly", "Mime Jr.", "Happiny", "Chatot", "Spiritomb", "Gible", "Gabite", "Garchomp", "Munchlax",
 "Riolu", "Lucario", "Hippopotas", "Hippowdon", "Skorupi", "Drapion", "Croagunk", "Toxicroak", "Carnivine", "Finneon",
 "Lumineon", "Mantyke", "Snover", "Abomasnow", "Weavile", "Magnezone", "Lickilicky", "Rhyperior", "Tangrowth",
 "Electivire", "Magmortar", "Togekiss", "Yanmega", "Leafeon", "Glaceon", "Gliscor", "Mamoswine", "Porygon-Z",
 "Gallade", "Probopass", "Dusknoir", "Froslass", "Rotom", "Uxie", "Mesprit", "Azelf", "Dialga", "Palkia", "Heatran",
 "Regigigas", "Giratina", "Cresselia", "Phione", "Manaphy", "Darkrai", "Shaymin", "Arceus"}

local abilityNamesList = {
 -- Gen 3
 "Stench", "Drizzle", "Speed Boost", "Battle Armor", "Sturdy", "Damp", "Limber", "Sand Veil", "Static",
 "Volt Absorb", "Water Absorb", "Oblivious", "Cloud Nine", "Compound Eyes", "Insomnia", "Color Change", "Immunity",
 "Flash Fire", "Shield Dust", "Own Tempo", "Suction Cups", "Intimidate", "Shadow Tag", "Rough Skin", "Wonder Guard",
 "Levitate", "Effect Spore", "Synchronize", "Clear Body", "Natural Cure", "Lightning Rod", "Serene Grace",
 "Swift Swim", "Chlorophyll", "Illuminate", "Trace", "Huge Power", "Poison Point", "Inner Focus", "Magma Armor",
 "Water Veil", "Magnet Pull", "Soundproof", "Rain Dish", "Sand Stream", "Pressure", "Thick Fat", "Early Bird",
 "Flame Body", "Run Away", "Keen Eye", "Hyper Cutter", "Pickup", "Truant", "Hustle", "Cute Charm", "Plus", "Minus",
 "Forecast", "Sticky Hold", "Shed Skin", "Guts", "Marvel Scale", "Liquid Ooze", "Overgrow", "Blaze", "Torrent",
 "Swarm", "Rock Head", "Drought", "Arena Trap", "Vital Spirit", "White Smoke", "Pure Power", "Shell Armor",
 "Air Lock",
 -- Gen 4
 "Tangled Feet", "Motor Drive", "Rivalry", "Steadfast", "Snow Cloak", "Gluttony", "Anger Point", "Unburden",
 "Heatproof", "Simple", "Dry Skin", "Download", "Iron Fist", "Poison Heal", "Adaptability", "Skill Link", "Hydration",
 "Solar Power", "Quick Feet", "Normalize", "Sniper", "Magic Guard", "No Guard", "Stall", "Technician", "Leaf Guard",
 "Klutz", "Mold Breaker", "Super Luck", "Aftermath", "Anticipation", "Forewarn", "Unaware", "Tinted Lens", "Filter",
 "Slow Start", "Scrappy", "Storm Drain", "Ice Body", "Solid Rock", "Snow Warning", "Honey Gather", "Frisk", "Reckless",
 "Multitype", "Flower Gift", "Bad Dreams"}

local itemNamesList = {
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

---------------------------------------------------------------------------------------------------
-- Gioco e indirizzi
---------------------------------------------------------------------------------------------------

-- Spostamento degli indirizzi rispetto alla versione inglese; puntatore "pid" del tuo Pt_RNG_DeSmuMe.lua
local LANGUAGES = {
 [0x45] = { name = "ENG", offset = 0x0,    pidPointer = 0x2101D2C, korean = 0 },
 [0x49] = { name = "ITA", offset = 0x160,  pidPointer = 0x2101E8C, korean = 0 },
 [0x46] = { name = "FRA", offset = 0x1E0,  pidPointer = 0x2101F0C, korean = 0 },
 [0x44] = { name = "GER", offset = 0x1A0,  pidPointer = 0x2101ECC, korean = 0 },
 [0x53] = { name = "SPA", offset = 0x200,  pidPointer = 0x2101F2C, korean = 0 },
 [0x4A] = { name = "JPN", offset = -0xC00, pidPointer = 0x210112C, korean = 0 },
 [0x4B] = { name = "KOR", offset = 0xF00,  pidPointer = 0x2102C2C, korean = 0x44 },
}

local gameCode = read32(0x2FFFE0C)
local language = LANGUAGES[floor(gameCode / 0x1000000)]

if band(gameCode, 0xFFFFFF) ~= 0x555043 or not language then
 print("Gioco non supportato: serve Pokémon Platino (ITA/ENG/FRA/GER/SPA/JPN/KOR).")
 return
end

-- Indirizzi (versione inglese + offset della lingua). Fonti: Pt_RNG_DeSmuMe.lua e pokebot-nds (wyanido).
local ANCHOR_PTR = 0x21C0794 + language.offset      -- puntatore ai dati del giocatore
local BATTLE_INDICATOR = 0x21D18F2 + language.offset -- 0x41 / 0x97 / 0xC0 durante una lotta
local FACING_OFFSET = 0x238A4                        -- dall'anchor: 0 su, 1 giù, 2 sinistra, 3 destra
local MON_SIZE = 0xEC                                -- Pokémon in squadra / in lotta: 236 byte

local function validPointer(p)
 return p >= 0x2000000 and p < 0x2400000
end

local function getAnchor()
 local anchor = read32(ANCHOR_PTR)
 return validPointer(anchor) and anchor or nil
end

local function inBattle()
 local v = read8(BATTLE_INDICATOR)
 return v == 0x41 or v == 0x97 or v == 0xC0
end

local function getTrainerIds()
 local anchor = getAnchor()
 if not anchor then return 0, 0 end
 return read16(anchor + 0x8C), read16(anchor + 0x8E)
end

---------------------------------------------------------------------------------------------------
-- Lettura dei dati di un Pokémon (cifrati, con verifica del checksum)
---------------------------------------------------------------------------------------------------

local function lcrng(s)
 local lo = s % 0x10000
 local hi = floor(s / 0x10000)
 local mid = (0x41C6 * lo + 0x4E6D * hi) % 0x10000
 return (0x4E6D * lo + mid * 0x10000 + 0x6073) % 0x100000000
end

-- Ordine dei 4 blocchi (A = crescita, B = mosse/IV, C, D) per ognuno dei 24 valori
local BLOCK_ORDERS = {
 "ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC", "BCAD", "BCDA", "BDAC", "BDCA",
 "CABD", "CADB", "CBAD", "CBDA", "CDAB", "CDBA", "DABC", "DACB", "DBAC", "DBCA", "DCAB", "DCBA"}

local BLOCK_POS = {}
for i, order in ipairs(BLOCK_ORDERS) do
 BLOCK_POS[i - 1] = { A = order:find("A") - 1, B = order:find("B") - 1 }
end

-- Restituisce una tabella con i dati del Pokémon, oppure nil se all'indirizzo non c'è un Pokémon valido
local function readPokemon(addr)
 local pid = read32(addr)
 local checksum = read16(addr + 6)
 if pid == 0 and checksum == 0 then return nil end

 local raw = {}
 for i = 0, 63 do raw[i] = read16(addr + 8 + i * 2) end

 local words, sum = {}, 0
 local seed = checksum
 for i = 0, 63 do
  seed = lcrng(seed)
  local w = bxor(raw[i], floor(seed / 0x10000)) % 0x10000
  words[i] = w
  sum = sum + w
 end

 if sum % 0x10000 ~= checksum then
  -- Alcuni Pokémon di Platino restano in chiaro per qualche istante: prova senza decifrare
  sum = 0
  for i = 0, 63 do sum = sum + raw[i] end
  if sum % 0x10000 ~= checksum then return nil end
  words = raw
 end

 local pos = BLOCK_POS[floor(band(pid, 0x3E000) / 0x2000) % 24]
 local a = pos.A * 16
 local b = pos.B * 16

 local species = words[a]
 if species < 1 or species > 493 then return nil end

 local ivs = words[b + 8] + words[b + 9] * 0x10000
 local function iv(shift) return floor(ivs / 2 ^ shift) % 32 end

 return {
  pid = pid,
  species = species,
  item = words[a + 1],
  otId = words[a + 2],
  otSid = words[a + 3],
  ability = floor(words[a + 6] / 256),
  nature = pid % 25,                  -- 0..24
  iv = { ps = iv(0), att = iv(5), dif = iv(10), vel = iv(15), attsp = iv(20), difsp = iv(25) },
  isEgg = floor(ivs / 2 ^ 30) % 2 == 1,
 }
end

local function isShiny(mon, tid, sid)
 return bxor(bxor(tid, sid), bxor(mon.pid % 0x10000, floor(mon.pid / 0x10000))) < 8
end

-- Pokémon avversari in lotta (1 o 2), con il puntatore di pokebot-nds. Solo se quella catena di puntatori non
-- esiste si usa l'indirizzo del tuo Pt_RNG_DeSmuMe.lua (lì possono restare i dati della lotta precedente).
local function readFoes()
 local foes = {}
 local anchor = getAnchor()
 local foeAnchor = anchor and read32(anchor + 0x217A8) or 0

 if validPointer(foeAnchor) then
  local count = read8(foeAnchor - 0x2D5C)
  if count >= 1 and count <= 2 then
   for i = 1, count do
    local mon = readPokemon(foeAnchor - 0x2D58 + (i - 1) * MON_SIZE)
    if not mon then return {} end
    table.insert(foes, mon)
   end
  end
  return foes
 end

 local base = read32(language.pidPointer)
 if validPointer(base) then
  local mon = readPokemon(base + 0x58E3C + language.korean)
  if mon then foes = { mon } end
 end
 return foes
end

local function readParty()
 local anchor = getAnchor()
 if not anchor then return nil end
 local count = read8(anchor + 0xB0)
 if count < 1 or count > 6 then return nil end
 return count, anchor + 0xB4
end

---------------------------------------------------------------------------------------------------
-- Nomi e confronto con i bersagli
---------------------------------------------------------------------------------------------------

local function normalize(s)
 s = string.lower(tostring(s))
 s = s:gsub("\195\169", "e"):gsub("\195\168", "e"):gsub("\195\160", "a"):gsub("\195\172", "i")
      :gsub("\195\178", "o"):gsub("\195\185", "u")
 return (s:gsub("[%s%.%-'’_]", ""))
end

local function buildIndex(list, firstId)
 local index = {}
 for i, name in ipairs(list) do index[normalize(name)] = i - 1 + firstId end
 return index
end

local speciesIndex = buildIndex(speciesNamesList, 1)   -- Bulbasaur = 1
local itemIndex = buildIndex(itemNamesList, 0)         -- None = 0
local abilityIndex = buildIndex(abilityNamesList, 1)   -- Stench = 1
local natureIndex = buildIndex(natureNamesEn, 0)
for k, v in pairs(buildIndex(natureNamesIt, 0)) do natureIndex[k] = v end

local function speciesName(id) return speciesNamesList[id] or ("#" .. id) end
local function itemName(id) return id == 0 and "nessuno" or (itemNamesList[id + 1] or ("#" .. id)) end
local function abilityName(id) return abilityNamesList[id] or ("#" .. id) end
local function natureName(n) return natureNamesIt[n + 1] end

local configErrors = {}

-- Trasforma un valore (nome, numero o lista) della configurazione in un insieme di numeri
local function toSet(value, index, what)
 if value == nil then return nil end
 local list = type(value) == "table" and value or { value }
 local set = {}
 for _, v in ipairs(list) do
  local id = type(v) == "number" and v or index[normalize(v)]
  if id == nil then
   table.insert(configErrors, what .. " sconosciuto: \"" .. tostring(v) .. "\"")
  else
   set[id] = true
  end
 end
 return set
end

local IV_KEYS = { "ps", "att", "dif", "attsp", "difsp", "vel" }

if CERCA_STRUMENTI_SELVATICI then
 for _, p in ipairs(STRUMENTI_SELVATICI) do table.insert(BERSAGLI, p) end
end
if FERMA_SE_SHINY then
 table.insert(BERSAGLI, { shiny = true })   -- nessuna altra condizione: vale per ogni specie
end

local profiles = {}
for n, p in ipairs(BERSAGLI) do
 local profile = {
  species = toSet(p.specie, speciesIndex, "Specie"),
  shiny = p.shiny,
  nature = toSet(p.natura, natureIndex, "Natura"),
  ability = toSet(p.abilita, abilityIndex, "Abilità"),
  ivMin = p.iv_min or {},
  ivMax = p.iv_max or {},
 }
 if p.strumento ~= nil and normalize(p.strumento) == "qualsiasi" then
  profile.anyItem = true
 else
  profile.item = toSet(p.strumento, itemIndex, "Strumento")
 end
 for _, t in ipairs({ profile.ivMin, profile.ivMax }) do
  for k in pairs(t) do
   local ok = false
   for _, key in ipairs(IV_KEYS) do ok = ok or key == k end
   if not ok then table.insert(configErrors, "IV sconosciuto nel profilo " .. n .. ": \"" .. tostring(k) .. "\" (usa ps, att, dif, attsp, difsp, vel)") end
  end
 end
 table.insert(profiles, profile)
end

if #profiles == 0 then table.insert(configErrors, "BERSAGLI è vuoto") end
if MOVIMENTO ~= "gira" and MOVIMENTO ~= "cammina" then
 table.insert(configErrors, "MOVIMENTO deve essere \"gira\" o \"cammina\"")
end
if MODALITA ~= "selvatici" and MODALITA ~= "fisso" and MODALITA ~= "regalo" then
 table.insert(configErrors, "MODALITA deve essere \"selvatici\", \"fisso\" o \"regalo\"")
end

if #configErrors > 0 then
 for _, e in ipairs(configErrors) do print("[Configurazione] " .. e) end
 print("Correggi la configurazione in cima allo script e ricaricalo.")
 return
end

local function matchesProfile(mon, shiny, p)
 if p.species and not p.species[mon.species] then return false end
 if p.anyItem and mon.item == 0 then return false end
 if p.item and not p.item[mon.item] then return false end
 if p.shiny ~= nil and p.shiny ~= shiny then return false end
 if p.nature and not p.nature[mon.nature] then return false end
 if p.ability and not p.ability[mon.ability] then return false end
 for k, v in pairs(p.ivMin) do if mon.iv[k] < v then return false end end
 for k, v in pairs(p.ivMax) do if mon.iv[k] > v then return false end end
 return true
end

local function isTarget(mon, shiny)
 for i, p in ipairs(profiles) do
  if matchesProfile(mon, shiny, p) then return i end
 end
 return nil
end

-- Ordine: PS / Attacco / Difesa / Attacco Speciale / Difesa Speciale / Velocità
local function ivText(iv)
 return string.format("%d/%d/%d/%d/%d/%d", iv.ps, iv.att, iv.dif, iv.attsp, iv.difsp, iv.vel)
end

local function describe(mon, shiny)
 return string.format("%s%s - %s - %s - %s - IV %s", speciesName(mon.species), shiny and " SHINY" or "",
  natureName(mon.nature), itemName(mon.item), abilityName(mon.ability), ivText(mon.iv))
end

---------------------------------------------------------------------------------------------------
-- Stato, pannello e registro
---------------------------------------------------------------------------------------------------

local state = {
 status = "avvio",
 encounters = 0,
 resets = 0,
 startFrame = emu.framecount(),
 last = nil, lastShiny = false,
 found = nil, foundShiny = false, foundProfile = nil,
 paused = false,
 panel = MOSTRA_PANNELLO,
 diag = false,
 error = nil,
}

local function setStatus(s) state.status = s end

local function logEncounter(mon, shiny, target)
 if not SALVA_REGISTRO then return end
 local f = io.open("incontri.csv", "a")
 if not f then return end
 if f:seek("end") == 0 then
  f:write("data;modalita;specie;shiny;natura;strumento;abilita;ps;att;dif;attsp;difsp;vel;pid;bersaglio\n")
 end
 f:write(string.format("%s;%s;%s;%s;%s;%s;%s;%d;%d;%d;%d;%d;%d;%08X;%s\n", os.date("%Y-%m-%d %H:%M:%S"), MODALITA,
  speciesName(mon.species), shiny and "si" or "no", natureName(mon.nature), itemName(mon.item), abilityName(mon.ability),
  mon.iv.ps, mon.iv.att, mon.iv.dif, mon.iv.attsp, mon.iv.difsp, mon.iv.vel, mon.pid, target and "SI" or ""))
 f:close()
end

local function elapsedText()
 local s = floor(((state.endFrame or emu.framecount()) - state.startFrame) / 60)
 return string.format("%d:%02d:%02d", floor(s / 3600), floor(s / 60) % 60, s % 60)
end

local function drawPanel()
 if not state.panel then return end

 local lines = {}
 local colors = {}
 local function add(text, color) table.insert(lines, text) ; colors[#lines] = color end

 add(string.format("Autoplay Pt (%s) - %s   [1] pausa", language.name, MODALITA), "#FFE070")
 if MODALITA == "selvatici" then
  add(string.format("Incontri: %d   Tempo: %s", state.encounters, elapsedText()))
 else
  add(string.format("Tentativi: %d   Tempo: %s", state.resets, elapsedText()))
 end

 local mon = state.found or state.last
 local shiny = state.found and state.foundShiny or state.lastShiny
 if mon then
  add((state.found and "TROVATO: " or "Ultimo: ") .. speciesName(mon.species) .. (shiny and " (shiny)" or ""),
   state.found and "#60FF60" or (shiny and "#60FF60" or nil))
  add("Natura: " .. natureName(mon.nature) .. "  Strum.: " .. itemName(mon.item))
  add("IV: " .. ivText(mon.iv) .. "  " .. abilityName(mon.ability))   -- PS/Att/Dif/AttSp/DifSp/Vel
 end

 if state.error then
  add(state.error, "#FF6060")
  add("[4] riparti", "#FF6060")
 elseif state.found then
  add((MODALITA == "regalo" and "Fermo. Salva la partita!" or "Fermo: tocca a te!") .. "  [4] riparti", "#60FF60")
 elseif state.paused then
  add("IN PAUSA - tasto 1 per riprendere", "#FFA040")
 else
  add("Stato: " .. state.status)
 end

 -- Se il primo incontro non viene riconosciuto, qui si vede quale lettura non torna
 if state.diag then
  local anchor = getAnchor()
  local foeAnchor = anchor and read32(anchor + 0x217A8) or 0
  local foeCount = validPointer(foeAnchor) and tostring(read8(foeAnchor - 0x2D5C)) or "no"
  add(string.format("Diag: lotta %02X  gioc. %s  avv. %s", read8(BATTLE_INDICATOR),
   anchor and string.format("%08X", anchor) or "no", foeCount), "#80C0FF")
 end

 local width = 0
 for _, l in ipairs(lines) do
  local _, cont = l:gsub("[\128-\191]", "")
  width = math.max(width, (#l - cont) * 6)
 end

 gui.box(1, -191, width + 6, -191 + #lines * 10 + 3, "#000000B0", "#000000B0")
 for i, l in ipairs(lines) do
  gui.text(4, -189 + (i - 1) * 10, l, colors[i] or "#FFFFFF")
 end
end

gui.register(drawPanel)

---------------------------------------------------------------------------------------------------
-- Input
---------------------------------------------------------------------------------------------------

local held = {}      -- tasti tenuti premuti tra un frame e l'altro
local prevKeys = {}
local suspended = false   -- true quando lo script ha finito: non tocca più l'input

local function readScriptKeys()
 local keys = input.get()
 local function pressed(k) return keys[k] and not prevKeys[k] end
 if pressed("1") or pressed("numpad1") then
  if not state.found and not state.error then state.paused = not state.paused end
 end
 if pressed("2") or pressed("numpad2") then state.panel = not state.panel end
 if pressed("3") or pressed("numpad3") then state.diag = not state.diag end
 if pressed("4") or pressed("numpad4") then
  if state.found or state.error then state.restart = true end
 end
 prevKeys = keys
end

-- Avanza di un frame applicando i tasti tenuti (+ quelli extra solo per questo frame)
local function frame(extra)
 if not suspended then
  local buttons = {}
  for k, v in pairs(held) do buttons[k] = v end
  if extra then for k, v in pairs(extra) do buttons[k] = v end end
  joypad.set(buttons)
 end
 emu.frameadvance()
 readScriptKeys()

 -- In pausa: nessun input dallo script, giochi tu
 while state.paused do
  emu.frameadvance()
  readScriptKeys()
 end
end

local function wait(frames, extra)
 for _ = 1, frames do frame(extra) end
end

local function press(button, frames)
 wait(frames or 6, { [button] = true })
 frame()   -- un frame di rilascio, così il gioco vede la pressione successiva
end

local function touch(x, y, frames)
 for _ = 1, frames or 4 do
  stylus.set({ x = x, y = y, touch = true })
  frame()
 end
 stylus.set({ touch = false })
 frame()
end

local function releaseAll()
 held = {}
 joypad.set({})
end

local function stopWithTarget(mon, shiny, profile)
 releaseAll()
 suspended = true
 state.endFrame = emu.framecount()
 state.found, state.foundShiny, state.foundProfile = mon, shiny, profile
 print(string.format(">>> TROVATO (profilo %d): %s", profile, describe(mon, shiny)))
 local f = io.open("trovati.txt", "a")
 if f then
  f:write(os.date("%Y-%m-%d %H:%M:%S") .. "  " .. MODALITA .. "  " .. describe(mon, shiny) ..
   string.format("  PID %08X\n", mon.pid))
  f:close()
 end
end

local function stopWithError(message)
 releaseAll()
 suspended = true
 state.endFrame = emu.framecount()
 state.error = message
 print("[Fermo] " .. message)
end

---------------------------------------------------------------------------------------------------
-- Modalità "selvatici"
---------------------------------------------------------------------------------------------------

-- Aspetta i dati degli avversari. "previousPid" evita di rileggere il Pokémon della lotta precedente se la
-- memoria non è ancora stata aggiornata (dopo un po' lo accetta comunque).
local function waitForFoes(timeout, previousPid)
 for elapsed = 1, timeout do
  local foes = readFoes()
  if #foes > 0 and (foes[1].pid ~= previousPid or elapsed > 180) then return foes end
  if not inBattle() then return nil end
  frame()
 end
 return nil
end

-- Gira su sé stesso finché non inizia una lotta (come pokebot-nds). Ogni pressione breve cambia solo la
-- direzione; la sequenza giù-sinistra-su-destra non preme mai la direzione verso cui si è già girati,
-- che farebbe fare un passo.
local function spinUntilBattle()
 setStatus("giro su me stesso nell'erba")
 held = {}
 local anchor = getAnchor()
 if anchor and read8(anchor + FACING_OFFSET) == 1 then
  press("right", 4)
  wait(3)
 end
 while not inBattle() do
  for _, direction in ipairs({ "down", "left", "up", "right" }) do
   press(direction, 4)
   wait(3)
   if inBattle() then break end
  end
 end
end

-- Cammina avanti e indietro finché non inizia una lotta
local function walkUntilBattle()
 local dir1, dir2 = "left", "right"
 if DIREZIONE == "verticale" then dir1, dir2 = "up", "down" end

 setStatus("cammino nell'erba")
 held = { B = CORRI or nil }
 local direction = dir1
 while not inBattle() do
  held[dir1], held[dir2] = nil, nil
  held[direction] = true
  for _ = 1, FRAME_PER_PASSO do
   frame()
   if inBattle() then break end
  end
  direction = direction == dir1 and dir2 or dir1
 end
end

-- Tocca FUGA finché la lotta non finisce. false se non ci riesce (lo script si ferma)
local function fleeBattle()
 setStatus("fuga")
 local battleFrames = 0
 while inBattle() do
  touch(FUGA_X, FUGA_Y, 4)
  wait(5)
  battleFrames = battleFrames + 10
  if battleFrames > 60 * 60 then
   stopWithError("Non riesco a fuggire: controlla la lotta.")
   return false
  end
 end
 setStatus("ritorno in mappa")
 wait(90)
 return true
end

-- restarting = true quando riparte col tasto 4: la lotta in corso è quella del Pokémon già trovato, quindi fugge
local function runWild(restarting)
 if inBattle() then
  if restarting then
   print("Fuggo dalla lotta in corso e riprendo la ricerca.")
   if not fleeBattle() then return end
  else
   print("Sei già in lotta: controllo questo Pokémon.")
  end
 end

 while true do
  if MOVIMENTO == "gira" then spinUntilBattle() else walkUntilBattle() end
  releaseAll()

  -- Legge l'avversario (o i due avversari)
  setStatus("lotta: leggo i dati")
  local foes = waitForFoes(600, state.last and state.last.pid)
  if foes then
   state.encounters = state.encounters + 1
   local tid, sid = getTrainerIds()
   for _, mon in ipairs(foes) do
    local shiny = isShiny(mon, tid, sid)
    local profile = isTarget(mon, shiny)
    state.last, state.lastShiny = mon, shiny
    logEncounter(mon, shiny, profile)
    print(string.format("#%d %s", state.encounters, describe(mon, shiny)))
    if profile then
     stopWithTarget(mon, shiny, profile)
     return
    end
   end
  else
   print("Lotta senza dati leggibili, provo a fuggire.")
  end

  if not fleeBattle() then return end
 end
end

---------------------------------------------------------------------------------------------------
-- Modalità "fisso" e "regalo"
---------------------------------------------------------------------------------------------------

local function softReset()
 releaseAll()
 setStatus("soft reset")
 if RESET_CON == "tasti" then
  wait(10, { L = true, R = true, start = true, select = true })
  frame()
 else
  emu.reset()
  frame()
 end
 -- Schermata bianca iniziale + ritardo casuale: cambia il seed iniziale a ogni tentativo
 wait(200)
 wait(math.random(100, 500))
end

-- Preme A con tempi irregolari (come pokebot-nds) finché condition() non è vera, o fino a "timeout" frame
local function mashUntil(condition, timeout)
 local elapsed = 0
 while elapsed < timeout do
  if DIREZIONE_FISSO then held = { [DIREZIONE_FISSO] = true } end
  local hold = math.random(5, 20)
  for _ = 1, hold do
   frame({ A = true })
   elapsed = elapsed + 1
   if condition() then return true end
  end
  for _ = 1, 5 do
   frame()
   elapsed = elapsed + 1
   if condition() then return true end
  end
 end
 return false
end

local function checkCandidate(mon, shiny)
 state.last, state.lastShiny = mon, shiny
 local profile = isTarget(mon, shiny)
 logEncounter(mon, shiny, profile)
 print(string.format("#%d %s", state.resets, describe(mon, shiny)))
 if profile then
  stopWithTarget(mon, shiny, profile)
  return true
 end
 return false
end

local function runStatic()
 local failures = 0
 while true do
  softReset()
  state.resets = state.resets + 1
  setStatus("premo A fino alla lotta")

  local foes
  local ok = mashUntil(function()
   if not inBattle() then return false end
   foes = readFoes()
   return #foes > 0
  end, ATTESA_MAX)
  releaseAll()

  if ok then
   failures = 0
   local tid, sid = getTrainerIds()
   local mon = foes[1]
   if checkCandidate(mon, isShiny(mon, tid, sid)) then return end
  else
   failures = failures + 1
   print("Nessuna lotta dopo il reset (" .. failures .. "/3).")
   if failures >= 3 then
    stopWithError("Nessuna lotta dopo 3 reset: hai salvato davanti al Pokémon?")
    return
   end
  end
 end
end

local function runGift()
 local baseCount = readParty()
 if not baseCount then
  stopWithError("Non leggo la squadra: avvia lo script dentro la partita, davanti a chi fa il regalo.")
  return
 end
 if baseCount >= 6 then
  stopWithError("Squadra piena: lascia un posto libero per il regalo.")
  return
 end
 print("Pokémon in squadra adesso: " .. baseCount .. ". Aspetto il " .. (baseCount + 1) .. "°.")

 local failures = 0
 while true do
  softReset()
  state.resets = state.resets + 1
  setStatus("premo A fino al regalo")

  local gift
  local ok = mashUntil(function()
   local count, partyAddr = readParty()
   if count ~= baseCount + 1 then return false end
   gift = readPokemon(partyAddr + baseCount * MON_SIZE)
   return gift ~= nil
  end, ATTESA_MAX)
  releaseAll()

  if ok then
   failures = 0
   -- Il regalo ha già l'ID allenatore del giocatore
   if checkCandidate(gift, isShiny(gift, gift.otId, gift.otSid)) then return end
  else
   failures = failures + 1
   print("Nessun regalo dopo il reset (" .. failures .. "/3).")
   if failures >= 3 then
    stopWithError("Nessun regalo dopo 3 reset: hai salvato davanti a chi lo dà?")
    return
   end
  end
 end
end

---------------------------------------------------------------------------------------------------
-- Avvio
---------------------------------------------------------------------------------------------------

math.randomseed(os.time())
math.random()

print(string.format("Autoplay Platino (%s) - modalità %s - %d profili", language.name, MODALITA, #profiles))
print("Tasto 1 = pausa, 2 = pannello, 4 = riparti dopo una fermata. Usa l'avanti veloce per andare più rapido.")

local restarting = false
while true do
 if MODALITA == "selvatici" then
  runWild(restarting)
 elseif MODALITA == "fisso" then
  runStatic()
 else
  runGift()
 end

 -- Fermo: lo script mostra solo il pannello e l'input torna tutto a te, finché non premi 4
 releaseAll()
 suspended = true
 state.restart = false
 while not state.restart do
  emu.frameadvance()
  readScriptKeys()
 end

 print("Riprendo la ricerca.")
 state.restart = false
 state.found, state.foundShiny, state.foundProfile = nil, false, nil
 state.error, state.endFrame = nil, nil
 suspended = false
 restarting = true
end

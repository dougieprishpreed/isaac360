"""Pack the original room/player/enemy/HUD pixels for the target viewport.

The Xbox 360 D3DX image path produced tiled-memory sampling artifacts, so the
port ships a prepared linear RGBA atlas exactly like the frontend one. Pixels
are copied from the inherited original PNGs without replacement artwork.
Enemy entries are first-frame compositions of their original ANM2 layers.
"""
from pathlib import Path
import hashlib,io,json,struct
import xml.etree.ElementTree as ET
from PIL import Image
from original_resources import OriginalResources

ROOT=Path(__file__).resolve().parents[1]
WIDTH=2048
HEIGHT=4096
ENEMY_SIZE=64
ENEMY_ANCHOR=(32,48)

IMAGES=[
    ('backdrop1','gfx/backdrop/01_basement.png'),
    ('backdrop2','gfx/backdrop/02_cellar.png'),
    ('backdrop4','gfx/backdrop/03_Caves.png'),
    ('backdrop5','gfx/backdrop/04_Catacombs.png'),
    ('backdrop7','gfx/backdrop/05_Depths.png'),
    ('backdrop8','gfx/backdrop/06_Necropolis.png'),
    ('backdrop10','gfx/backdrop/07_The Womb.png'),
    ('backdrop11','gfx/backdrop/08_Utero.png'),
    ('backdrop14','gfx/backdrop/09_Sheol.png'),
    ('backdrop16','gfx/backdrop/12_DarkRoom.png'),
    ('backdrop17','gfx/backdrop/11_Chest.png'),
    ('backdrop15','gfx/backdrop/10_Cathedral.png'),
    ('player','gfx/characters/costumes/character_001_isaac.png'),
    ('hearts','gfx/ui/ui_hearts.png'),
    ('tears','gfx/tears.png'),
    ('bodies01','gfx/monsters/classic/monster_000_bodies01.png'),
    ('frowninggaper','gfx/monsters/classic/monster_024_frowninggaper.png'),
    ('flysheet','gfx/monsters/classic/monster_010_fly.png'),
    ('pootersheet','gfx/monsters/classic/monster_001_pooter.png'),
    ('rocksbasement','gfx/grid/rocks_basement.png'),
    ('doornormal','gfx/grid/door_01_normaldoor.png'),
    ('fattysheet','gfx/monsters/rebirth/monster_207_fatty.png'),
    ('poopsheet','gfx/grid/grid_poop_1.png'),
    ('fireplacesheet','gfx/grid/grid_fireplace.png'),
    ('pickupheart','gfx/items/pick ups/pickup_001_heart.png'),
    ('pickupcoin','gfx/items/pick ups/pickup_002_coin.png'),
    ('pickupkey','gfx/items/pick ups/pickup_003_key.png'),
    ('pickupbomb','gfx/items/pick ups/pickup_016_bomb.png'),
]
ACTORS=[
    ('gaper','gfx/010.000_Frowning Gaper.anm2','WalkHori','Head'),
    ('fly','gfx/013.000_Fly.anm2','Fly',None),
    ('pooter','gfx/014.000_Pooter.anm2','Fly','Attack'),
    ('fatty','gfx/207.000_Fatty.anm2','WalkHori','AttackVert'),
    ('skinny','gfx/226.000_Skinny.anm2','WalkHori','Head'),
    ('rotty','gfx/226.001_Rotty.anm2','WalkHori','Head'),
    ('cyclopia','gfx/284.000_Cyclopia.anm2','WalkHori','Head'),
]
#Walk cycles packed as horizontal strips. The fifth value is the square cell
#size; large bosses use128. Frame counts come from the original ANM2 tracks.
ANIMATED=[
    ('gaperwalk','gfx/010.000_Frowning Gaper.anm2','WalkHori','Head',64),
    ('flywalk','gfx/013.000_Fly.anm2','Fly',None,64),
    ('pooterwalk','gfx/014.000_Pooter.anm2','Fly','Attack',64),
    ('fattywalk','gfx/207.000_Fatty.anm2','WalkHori','AttackVert',64),
    ('skinnywalk','gfx/226.000_Skinny.anm2','WalkHori','Head',64),
    ('rottywalk','gfx/226.001_Rotty.anm2','WalkHori','Head',64),
    ('cyclopiawalk','gfx/284.000_Cyclopia.anm2','WalkHori','Head',64),
    ('dinglewalk','gfx/261.000_Dingle.anm2','Slide',None,64),
    ('dartflywalk','gfx/256.000_DartFly.anm2','Fly',None,64),
    ('dipwalk','gfx/216.000_Dip.anm2','Move',None,64),
    ('cornwalk','gfx/216.001_Corn.anm2','Move',None,64),
    ('browniecornwalk','gfx/216.002_BrownieCorn.anm2','Move',None,64),
    ('bigcornwalk','gfx/216.003_Big Corn.anm2','Move',None,64),
    ('danglewalk','gfx/261.001_Dangle.anm2','Slide',None,64),
    ('larrywalk','gfx/019.000_Larry Jr..anm2','WalkHeadHori',None,64),
    ('monstrowalk','gfx/020.000_Monstro.anm2','Walk',None,128),
    ('dukewalk','gfx/067.000_The Duke of Flies.anm2','Walk',None,128),
    ('hauntidle','gfx/260.000_Haunt.anm2','IdleSkin',None,128),
    ('trapdooropen','gfx/grid/Door_11_TrapDoor.anm2','Open Animation',None,64),
    ('attackflywalk','gfx/018.000_Attack Fly.anm2','Fly',None,64),
    ('geminiwalk','gfx/079.000_Gemini.anm2','WalkHori',None,64),
    ('gurglingwalk','gfx/237.000_Gurgling.anm2','WalkLeft',None,64),
    ('turdlingwalk','gfx/237.002_Turdling.anm2','WalkLeft',None,64),
    ('grubwalk','gfx/239.000_Grub.anm2','WalkBodyHori',None,64),
    ('plumidle','gfx/908.000_baby plum.anm2','Idle',None,64),
    ('maggotwalk','gfx/021.000_Maggot.anm2','',None,64),
    ('hivewalk','gfx/022.000_Hive.anm2','',None,64),
    ('chargerwalk','gfx/023.000_Charger.anm2','',None,64),
    ('globinwalk','gfx/024.000_Globin.anm2','',None,64),
    ('boomflywalk','gfx/025.000_Boom Fly.anm2','',None,64),
    ('mawwalk','gfx/026.000_Maw.anm2','',None,64),
    ('hostwalk','gfx/027.000_Host.anm2','',None,64),
    ('hopperwalk','gfx/029.000_Hopper.anm2','',None,64),
    ('boilwalk','gfx/030.000_Boil.anm2','',None,64),
    ('spittywalk','gfx/031.000_Spity.anm2','',None,64),
    ('suckerwalk','gfx/061.000_Sucker.anm2','',None,64),
    ('spiderwalk','gfx/085.000_Spider.anm2','',None,64),
    ('keeperwalk','gfx/086.000_Keeper.anm2','',None,64),
    ('gurglewalk','gfx/087.000_Gurgle.anm2','',None,64),
    ('walkingboilwalk','gfx/088.000_Walking Boil.anm2','',None,64),
    ('buttlickerwalk','gfx/089.000_Buttlicker.anm2','',None,64),
    ('bigspiderwalk','gfx/094.000_Big Spider.anm2','',None,64),
    ('squirtwalk','gfx/220.000_Squirt.anm2','',None,64),
    ('bonywalk','gfx/227.000_Boney.anm2','',None,64),
    ('oneteethwalk','gfx/234.000_OneTooth.anm2','',None,64),
    ('splasherwalk','gfx/238.000_Splasher.anm2','',None,64),
    ('wallcreepwalk','gfx/240.000_Wall Creep.anm2','',None,64),
    ('blindcreepwalk','gfx/242.000_Blind Creep.anm2','',None,64),
    ('conjoinedwalk','gfx/243.000_Conjoined Spitty.anm2','',None,64),
    ('roundwormwalk','gfx/244.000_Round Worm.anm2','',None,64),
    ('psychichorfwalk','gfx/248.000_Psychic Horf.anm2','',None,64),
    ('hive1walk','gfx/022.001_Drowned Hive.anm2','',None,64),
    ('charger1walk','gfx/023.001_Drowned Charger.anm2','',None,64),
    ('globin1walk','gfx/024.001_Gazing Globin.anm2','',None,64),
    ('boomfly1walk','gfx/025.001_Red Boom Fly.anm2','',None,64),
    ('boomfly2walk','gfx/025.002_Drowned Boom Fly.anm2','',None,64),
    ('maw1walk','gfx/026.001_Red Maw.anm2','',None,64),
    ('maw2walk','gfx/026.002_Psychic Maw.anm2','',None,64),
    ('host1walk','gfx/027.001_Red Host.anm2','',None,64),
    ('hopper1walk','gfx/029.001_Trite.anm2','',None,64),
    ('boil1walk','gfx/030.001_Gut.anm2','',None,64),
    ('boil2walk','gfx/030.002_Sack.anm2','',None,64),
    ('sucker1walk','gfx/061.001_Spit.anm2','',None,64),
    ('walkingboil1walk','gfx/088.001_Walking Gut.anm2','',None,64),
    ('walkingboil2walk','gfx/088.002_Walking Sack.anm2','',None,64),
    ('squirt1walk','gfx/220.001_Dank Squirt.anm2','',None,64),
    ('superpooterwalk','gfx/014.001_Super Pooter.anm2','',None,64),
    ('palefattywalk','gfx/207.001_Pale Fatty.anm2','',None,64),
    ('chubberwalk','gfx/039.002_Chubber.anm2','',None,64),
    ('mushroomwalk','gfx/300.000_MushroomMan.anm2','',None,64),
    ('chubwalk','gfx/028.000_Chub.anm2','',None,64),
    ('gurdywalk','gfx/036.000_Gurdy.anm2','',None,192),
    ('pinwalk','gfx/062.000_Pin.anm2','',None,128),
    ('peepwalk','gfx/068.000_Peep.anm2','',None,128),
    ('fistulawalk','gfx/071.000_Fistula Big.anm2','',None,128),
    ('gurdyjrwalk','gfx/099.000_Gurdy Jr..anm2','',None,128),
    ('megamawwalk','gfx/262.000_MegaMaw.anm2','',None,192),
    ('megafattywalk','gfx/264.000_Mega Fatty.anm2','',None,128),
    ('fireplace33walk','gfx/033.000_Fireplace.anm2','',None,64),
    ('bumbinowalk','gfx/916.000_bumbino.anm2','',None,128),
    ('brainwalk','gfx/032.000_Brain.anm2','',None,64),
    ('leaperwalk','gfx/034.000_Leaper.anm2','',None,64),
    ('mrmawwalk','gfx/035.000_Mr. Maw Body.anm2','',None,64),
    ('babywalk','gfx/038.000_Baby.anm2','',None,64),
    ('viswalk','gfx/039.000_Vis.anm2','',None,64),
    ('gutswalk','gfx/040.000_Guts.anm2','',None,64),
    ('knightwalk','gfx/041.000_Knight.anm2','',None,64),
    ('pokywalk','gfx/044.000_Poky.anm2','',None,64),
    ('hangerwalk','gfx/090.000_Hanger.anm2','',None,64),
    ('swarmerwalk','gfx/091.000_Swarmer.anm2','',None,64),
    ('heartwalk','gfx/092.000_Heart.anm2','',None,64),
    ('maskwalk','gfx/093.000_Mask.anm2','',None,64),
    ('fatsackwalk','gfx/208.000_Fatsack.anm2','',None,64),
    ('deathsheadwalk','gfx/212.000_DeathsHead.anm2','',None,64),
    ('wizoobwalk','gfx/219.000_Wizoob.anm2','',None,64),
    ('tumorwalk','gfx/229.000_Tumor.anm2','',None,64),
    ('gapingmawwalk','gfx/235.000_GapingMaw.anm2','',None,64),
    ('ragecreepwalk','gfx/241.000_Rage Creep.anm2','',None,64),
    ('floatingknightwalk','gfx/254.000_Floating Knight.anm2','',None,64),
    ('blackbonywalk','gfx/277.000_Black Boney.anm2','',None,64),
    ('blackglobinwalk','gfx/280.000_Black Globins Body.anm2','',None,64),
    ('lilhauntwalk','gfx/260.010_Lil Haunt.anm2','',None,64),
    ('hollowwalk','gfx/019.001_The Hollow.anm2','',None,64),
    ('bluefirewalk','gfx/033.002_Blue Fireplace.anm2','',None,64),
    ('teratomawalk','gfx/072.001_Teratoma Medium.anm2','',None,64),
    ('slidewalk','gfx/044.001_Slide.anm2','',None,64),
    ('monstro2walk','gfx/043.000_Monstro II.anm2','',None,128),
    ('momwalk','gfx/045.000_Mom.anm2','',None,128),
    ('gatewalk','gfx/263.000_MegaMaw2.anm2','',None,128),
    ('cagewalk','gfx/265.000_Mega Fatty 2.anm2','',None,192),
    ('reapcreepwalk','gfx/900.000_reap creep.anm2','',None,192),
    ('dankglobinwalk','gfx/024.002_Dank Globin.anm2','',None,64),
    ('purplefirewalk','gfx/033.003_Purple Fireplace.anm2','',None,64),
    ('doubleviswalk','gfx/039.001_Double Vis.anm2','',None,64),
    ('stonegrimacewalk','gfx/042.000_Stone Grimace.anm2','',None,64),
    ('vomitgrimacewalk','gfx/042.001_Vomit Grimace.anm2','',None,64),
    ('leechwalk','gfx/055.000_Leech.anm2','',None,64),
    ('fistulamediumwalk','gfx/072.000_Fistula Medium.anm2','',None,64),
    ('teratomasmallwalk','gfx/073.001_Teratoma Small.anm2','',None,64),
    ('moterwalk','gfx/080.000_Moter.anm2','',None,64),
    ('horsemanheadwalk','gfx/083.000_Headless Horsemans Head.anm2','',None,64),
    ('mobilehostwalk','gfx/204.000_Mobile Host.anm2','',None,64),
    ('nestwalk','gfx/205.000_Nest.anm2','',None,64),
    ('blubberwalk','gfx/209.000_Blubber.anm2','',None,64),
    ('halfsackwalk','gfx/210.000_Half Sack.anm2','',None,64),
    ('dankdeathsheadwalk','gfx/212.001_Dank DeathsHead.anm2','',None,64),
    ('momshandwalk','gfx/212.000_Moms Hand.anm2','',None,64),
    ('dingawalk','gfx/223.000_Dinga.anm2','',None,64),
    ('raglingwalk','gfx/246.000_Ragling.anm2','',None,64),
    ('nullswalk','gfx/252.000_Nulls.anm2','',None,64),
    ('fatbatwalk','gfx/258.000_Fat Bat.anm2','',None,64),
    ('megaclottywalk','gfx/282.000_MegaClotty.anm2','',None,64),
    ('dukiewalk','gfx/288.000_Dukie.anm2','',None,64),
    ('armyflywalk','gfx/868.000_army fly.anm2','',None,64),
    ('needlewalk','gfx/881.000_needle.anm2','',None,64),
    ('pastywalk','gfx/881.001_pasty.anm2','',None,64),
    ('mredmawwalk','gfx/035.002_Mr. Red Maw Body.anm2','',None,64),
    ('eggywalk','gfx/029.002_Eggy.anm2','',None,64),
    ('triplegrimacewalk','gfx/042.002_Triple Grimace.anm2','',None,64),
    ('stoneshooterwalk','gfx/202.000_Constant Stone Shooter.anm2','',None,64),
    ('crossshooterwalk','gfx/202.010_cross stone shooter.anm2','',None,64),
    ('nerveendingwalk','gfx/231.000_NerveEnding.anm2','',None,64),
    ('brokengapingwalk','gfx/236.000_Broken GapingMaw.anm2','',None,64),
    ('poisonmindwalk','gfx/301.000_PoisonMind.anm2','',None,64),
    ('blisterwalk','gfx/303.000_Blister.anm2','',None,64),
    ('thethingwalk','gfx/304.000_TheThing.anm2','',None,64),
    ('ministrowalk','gfx/305.000_Ministro.anm2','',None,64),
    ('portalwalk','gfx/306.000_Portal.anm2','',None,64),
    ('clottywalk','gfx/015.000_Clotty.anm2','',None,64),
    ('clotwalk','gfx/015.001_Clot.anm2','',None,64),
    ('grilledclottywalk','gfx/015.003_grilled clotty.anm2','',None,64),
    ('doplewalk','gfx/053.000_Dople.anm2','',None,64),
    ('lumpwalk','gfx/056.000_Lump.anm2','',None,64),
    ('membrainwalk','gfx/057.000_MemBrain.anm2','',None,64),
    ('mamagutswalk','gfx/057.001_Mama Guts.anm2','',None,64),
    ('parabitewalk','gfx/058.000_Para-Bite.anm2','',None,64),
    ('scarredparabitewalk','gfx/058.001_Scarred Para-Bite.anm2','',None,64),
    ('fredwalk','gfx/059.000_Fred.anm2','',None,64),
    ('eyewalk','gfx/060.000_Eye.anm2','',None,64),
    ('bloodshotteyewalk','gfx/060.001_Bloodshot Eye.anm2','',None,64),
    ('blastocystwalk','gfx/075.000_Blastocyst Medium.anm2','',None,64),
    ('blastocystsmaillwalk','gfx/076.000_Blastocyst Small.anm2','',None,64),
    ('embryowalk','gfx/077.000_Embryo.anm2','',None,64),
    ('swingerwalk','gfx/221.000_Swinger.anm2','',None,64),
    ('wallhuggerwalk','gfx/218.000_Wall Hugger.anm2','',None,64),
    ('codwormwalk','gfx/220.000_Cod Worm.anm2','',None,64),
    ('oobwalk','gfx/224.000_Oob.anm2','',None,64),
    ('homunculuswalk','gfx/228.000_Homunculus.anm2','',None,64),
    ('planetoidwalk','gfx/229.001_Planetoid.anm2','',None,64),
    ('fleshmobilehostwalk','gfx/247.000_Flesh Mobile Host.anm2','',None,64),
    ('fleshdeathsheadwalk','gfx/286.000_FleshDeathsHead.anm2','',None,64),
    ('meatballwalk','gfx/290.000_Meatball.anm2','',None,64),
    ('peepingfattywalk','gfx/835.000_peeper fatty.anm2','',None,64),
    ('floastwalk','gfx/859.000_floating host.anm2','',None,64),
    ('scolexwalk','gfx/062.001_Scolex.anm2','',None,128),
    ('frailwalk','gfx/062.002_TheFrail.anm2','',None,128),
    ('momsheartwalk','gfx/078.000_Moms Heart.anm2','',None,128),
    ('itliveswalk','gfx/078.001_It Lives.anm2','',None,128),
    ('mamagurdywalk','gfx/266.000_Mega Gurdy.anm2','',None,512),
    ('mrfredwalk','gfx/270.000_MegaFred.anm2','',None,128),
    ('blastocystbigwalk','gfx/074.000_Blastocyst Big.anm2','',None,128),
    ('fallenwalk','gfx/081.000_The Fallen.anm2','',None,128),
    ('kamikazeleechwalk','gfx/055.001_Kamikaze Leech.anm2','',None,64),
    ('holyleechwalk','gfx/055.002_Holy Leech.anm2','',None,64),
    ('whipperwalk','gfx/834.000_whipper.anm2','',None,64),
    ('snapperwalk','gfx/834.001_snapper.anm2','',None,64),
    ('selflessknightwalk','gfx/041.001_Selfless Knight.anm2','',None,64),
    ('wrinklybabywalk','gfx/038.003_Wrinkly Baby.anm2','',None,64),
    ('angelicbabywalk','gfx/038.001_Angelic Baby.anm2','',None,64),
    ('soulsuckerwalk','gfx/061.002_soul sucker.anm2','',None,64),
    ('bulbwalk','gfx/061.005_bulb.anm2','',None,64),
    ('begottenwalk','gfx/251.000_Begotten.anm2','',None,64),
    ('boneknightwalk','gfx/283.000_BoneKnight.anm2','',None,64),
    ('impwalk','gfx/259.000_Imp.anm2','',None,64),
    ('redghostwalk','gfx/285.000_Red Ghost.anm2','',None,64),
    ('camillowalk','gfx/230.000_camillojr.anm2','',None,64),
    ('blackmawwalk','gfx/225.000_Black Maw.anm2','',None,64),
    ('blackglobinbodywalk','gfx/278.000_Black Globin.anm2','',None,64),
    ('brimstoneheadwalk','gfx/203.000_Brimstone Head.anm2','',None,64),
    ('greedwalk','gfx/050.000_Greed.anm2','',None,64),
    ('pridewalk','gfx/052.000_Pride.anm2','',None,64),
    ('eviltwinwalk','gfx/053.001_Evil Twin.anm2','',None,64),
    ('blackglobinheadwalk','gfx/279.000_Black Globins Head.anm2','',None,64),
    ('candlerwalk','gfx/833.000_candler.anm2','',None,64),
    ('poltywalk','gfx/816.000_polty.anm2','',None,64),
    ('bishopwalk','gfx/805.000_bishop.anm2','',None,64),
    ('psytumorwalk','gfx/253.000_PsyTumor.anm2','',None,64),
    ('holymulliganwalk','gfx/022.002_Holy Mulligan.anm2','',None,64),
    ('holyeyewalk','gfx/060.002_Holy Eye.anm2','',None,64),
    ('holybonywalk','gfx/227.001_Holy Bony.anm2','',None,64),
    ('lokiwalk','gfx/069.000_Loki.anm2','',None,128),
    ('widowwalk','gfx/100.000_Widow.anm2','',None,128),
    ('pitfallwalk','gfx/291.000_Pitfall.anm2','',None,64),
    ('iblobwalk','gfx/015.002_I.Blob.anm2','',None,64),
    ('satanwalk','gfx/084.000_Satan.anm2','',None,256),
    ('isaacwalk','gfx/102.000_Isaac (final boss).anm2','',None,64),
    ('mulliganwalk','gfx/016.000_Mulligan.anm2','',None,64),
    ('mulligoonwalk','gfx/016.001_Mulligoon.anm2','',None,64),
    ('mulliboomwalk','gfx/016.002_Mulliboom.anm2','',None,64),
    ('brotherbobbywalk','gfx/003.001_brother bobby.anm2','FloatSide',None,64),
    ('deadbirdwalk','gfx/characters/117_dead bird.anm2','HeadRight',None,64),
    ('lambwalk','gfx/273.000_TheLamb.anm2','',None,128),
    ('deliriumwalk','gfx/412.000_Delirium.anm2','',None,256),
    ('darkonewalk','gfx/267.000_Dark One.anm2','',None,128),
    ('adversarywalk','gfx/268.000_Dark One 2.anm2','',None,128),
    ('polycephaluswalk','gfx/269.000_Polycephalus.anm2','',None,128),
]
ANIMATION_FRAME_LIMIT=8
#Manifest strips that are enemy sprites, keyed by the native type/variant.
ENEMY_SPRITE_KEYS={
    'gaperwalk':(10,0),'flywalk':(13,0),'pooterwalk':(14,0),'fattywalk':(208,0),
    'skinnywalk':(226,0),'rottywalk':(226,1),'cyclopiawalk':(284,0),
    'dinglewalk':(261,0),'danglewalk':(261,1),'dartflywalk':(256,0),'dipwalk':(217,0),
    'larrywalk':(19,0),'monstrowalk':(20,0),'dukewalk':(67,0),'hauntidle':(260,0),
    'attackflywalk':(18,0),'geminiwalk':(79,0),'gurglingwalk':(237,1),'turdlingwalk':(237,2),
    'grubwalk':(239,0),'plumidle':(908,0),
    'maggotwalk':(21,0),'hivewalk':(22,0),'chargerwalk':(23,0),'globinwalk':(24,0),
    'boomflywalk':(25,0),'mawwalk':(26,0),'hostwalk':(27,0),'hopperwalk':(29,0),
    'boilwalk':(30,0),'spittywalk':(31,0),'suckerwalk':(61,0),'spiderwalk':(85,0),
    'keeperwalk':(86,0),'gurglewalk':(87,0),'walkingboilwalk':(88,0),'buttlickerwalk':(89,0),
    'bigspiderwalk':(94,0),'squirtwalk':(220,0),'bonywalk':(227,0),'oneteethwalk':(234,0),
    'splasherwalk':(238,0),'wallcreepwalk':(240,0),'blindcreepwalk':(242,0),
    'conjoinedwalk':(243,0),'roundwormwalk':(244,0),'psychichorfwalk':(248,0),
    'hive1walk':(22,1),'charger1walk':(23,1),'globin1walk':(24,1),'boomfly1walk':(25,1),
    'boomfly2walk':(25,2),'maw1walk':(26,1),'maw2walk':(26,2),'host1walk':(27,1),
    'hopper1walk':(29,1),'boil1walk':(30,1),'boil2walk':(30,2),'sucker1walk':(61,1),
    'walkingboil1walk':(88,1),'walkingboil2walk':(88,2),'squirt1walk':(220,1),
    'superpooterwalk':(14,1),'palefattywalk':(208,1),'chubberwalk':(39,2),'mushroomwalk':(300,0),
    'chubwalk':(28,0),'gurdywalk':(36,0),'pinwalk':(62,0),'peepwalk':(68,0),'fistulawalk':(71,0),
    'gurdyjrwalk':(99,0),'megamawwalk':(262,0),'megafattywalk':(264,0),'fireplace33walk':(33,0),'bumbinowalk':(916,0),
    'brainwalk':(32,0),'leaperwalk':(34,0),'mrmawwalk':(35,0),'babywalk':(38,0),'viswalk':(39,0),
    'gutswalk':(40,0),'knightwalk':(41,0),'pokywalk':(44,0),'hangerwalk':(90,0),'swarmerwalk':(91,0),
    'heartwalk':(92,0),'maskwalk':(93,0),'fatsackwalk':(209,0),'deathsheadwalk':(212,0),'wizoobwalk':(219,0),
    'tumorwalk':(229,0),'gapingmawwalk':(235,0),'ragecreepwalk':(241,0),'floatingknightwalk':(254,0),
    'blackbonywalk':(277,0),'blackglobinwalk':(280,0),'lilhauntwalk':(260,10),'hollowwalk':(19,1),
    'bluefirewalk':(33,2),'teratomawalk':(72,1),'slidewalk':(44,1),
    'monstro2walk':(43,0),'momwalk':(45,0),'gatewalk':(263,0),'cagewalk':(265,0),'reapcreepwalk':(900,0),
    'dankglobinwalk':(24,2),'purplefirewalk':(33,3),'doubleviswalk':(39,1),'stonegrimacewalk':(42,0),
    'vomitgrimacewalk':(42,1),'leechwalk':(55,0),'fistulamediumwalk':(72,0),'teratomasmallwalk':(73,1),
    'moterwalk':(80,0),'horsemanheadwalk':(83,0),'mobilehostwalk':(204,0),'nestwalk':(205,0),
    'blubberwalk':(210,0),'halfsackwalk':(211,0),'dankdeathsheadwalk':(212,1),'momshandwalk':(213,0),
    'dingawalk':(223,0),'raglingwalk':(246,0),'nullswalk':(252,0),'fatbatwalk':(258,0),
    'megaclottywalk':(282,0),'dukiewalk':(288,0),
    'armyflywalk':(868,0),'needlewalk':(881,0),'pastywalk':(881,1),'mredmawwalk':(35,2),
    'eggywalk':(29,2),'triplegrimacewalk':(42,2),'stoneshooterwalk':(202,0),'crossshooterwalk':(202,10),
    'nerveendingwalk':(231,0),'brokengapingwalk':(236,0),'poisonmindwalk':(301,0),'blisterwalk':(303,0),
    'thethingwalk':(304,0),'ministrowalk':(305,0),'portalwalk':(306,0),
    'clottywalk':(15,0),'clotwalk':(15,1),'grilledclottywalk':(15,3),'doplewalk':(53,0),
    'lumpwalk':(56,0),'membrainwalk':(57,0),'mamagutswalk':(57,1),'parabitewalk':(58,0),
    'scarredparabitewalk':(58,1),'fredwalk':(59,0),'eyewalk':(60,0),'bloodshotteyewalk':(60,1),
    'blastocystwalk':(75,0),'blastocystsmaillwalk':(76,0),'embryowalk':(77,0),'swingerwalk':(216,0),
    'wallhuggerwalk':(218,0),'codwormwalk':(221,0),'oobwalk':(224,0),'homunculuswalk':(228,0),
    'planetoidwalk':(229,1),'fleshmobilehostwalk':(247,0),'fleshdeathsheadwalk':(286,0),
    'meatballwalk':(290,0),'peepingfattywalk':(835,0),'floastwalk':(859,0),
    'scolexwalk':(62,1),'frailwalk':(62,2),'momsheartwalk':(78,0),'itliveswalk':(78,1),
    'mamagurdywalk':(266,0),'mrfredwalk':(270,0),'blastocystbigwalk':(74,0),'fallenwalk':(81,0),
    'kamikazeleechwalk':(55,1),'holyleechwalk':(55,2),'whipperwalk':(834,0),'snapperwalk':(834,1),
    'selflessknightwalk':(41,1),'wrinklybabywalk':(38,3),'angelicbabywalk':(38,1),'soulsuckerwalk':(61,2),
    'bulbwalk':(61,5),'begottenwalk':(251,0),'boneknightwalk':(283,0),'impwalk':(259,0),
    'redghostwalk':(285,0),'camillowalk':(230,0),'blackmawwalk':(225,0),'blackglobinbodywalk':(278,0),
    'brimstoneheadwalk':(203,0),'greedwalk':(50,0),'pridewalk':(52,0),'eviltwinwalk':(53,1),
    'blackglobinheadwalk':(279,0),'candlerwalk':(833,0),'poltywalk':(816,0),'bishopwalk':(805,0),
    'psytumorwalk':(253,0),'holymulliganwalk':(22,2),'holyeyewalk':(60,2),'holybonywalk':(227,1),
    'lokiwalk':(69,0),'widowwalk':(100,0),'pitfallwalk':(291,0),'iblobwalk':(15,2),
    'satanwalk':(84,0),'isaacwalk':(102,0),'mulliganwalk':(16,0),'mulligoonwalk':(16,1),'mulliboomwalk':(16,2),'lambwalk':(273,0),'deliriumwalk':(412,0),'darkonewalk':(267,0),'adversarywalk':(268,0),'polycephaluswalk':(269,0),
}
CORE4={'gaperwalk','flywalk','pooterwalk','fattywalk','skinnywalk','rottywalk','cyclopiawalk',
    'dinglewalk','danglewalk','dartflywalk','dipwalk','larrywalk','monstrowalk','dukewalk',
    'hauntidle','attackflywalk','geminiwalk','gurglingwalk','turdlingwalk','grubwalk','plumidle'}
#Late-floor actor strips use two original frames; the first-floor families keep
#four-frame cycles. This keeps the prepared atlas inside the 2048x4096 budget.
FRAME_LIMITS={name:(4 if name in CORE4 else 2) for name in ENEMY_SPRITE_KEYS}
FRAME_LIMITS['mamagurdywalk']=2
_sheet_cache={}
def load_image(resources,request):
    raw,entry=resources.read(request)
    return Image.open(io.BytesIO(raw)).convert('RGBA'),raw,entry
def tint(image,values):
    if values==(255,255,255,255):return image
    r,g,b,a=image.split();rr,gg,bb,aa=values
    r=r.point(lambda v,rr=rr:v*rr//255);g=g.point(lambda v,gg=gg:v*gg//255)
    b=b.point(lambda v,bb=bb:v*bb//255);a=a.point(lambda v,aa=aa:v*aa//255)
    return Image.merge('RGBA',(r,g,b,a))
def choose_animation(root,primary):
    names=[a.get('Name') for a in root.find('Animations')]
    if primary in names:return primary
    for candidate in ('WalkHori','Walk','Move','Fly','Swim','Idle','Appear','WalkLeft','WalkBodyHori','WalkHeadHori'):
        if candidate in names:return candidate
    return names[0] if names else primary
def compose_actor(resources,request,primary,overlay,frame_index=0,size=ENEMY_SIZE):
    raw,entry=resources.read(request);root=ET.fromstring(raw);primary=choose_animation(root,primary)
    sheets={s.get('Id'):s.get('Path') for s in root.find('Content/Spritesheets')}
    animations={a.get('Name'):a for a in root.find('Animations')}
    anchor=(size//2,int(size*0.75));canvas=Image.new('RGBA',(size,size),(0,0,0,0))
    used=[]
    for layer in root.find('Content/Layers'):
        frame=None
        for name in (primary,overlay):
            if not name or name not in animations:continue
            for track in animations[name].find('LayerAnimations'):
                if track.get('LayerId')==layer.get('Id'):
                    frames=list(track)
                    if frames:
                        frame=frames[min(frame_index,len(frames)-1)];used.append('%s/%s'%(name,layer.get('Name')));break
            if frame is not None:break
        if frame is None:continue
        sheet=sheets.get(layer.get('SpritesheetId'))
        if not sheet:raise ValueError('Missing sheet for '+request)
        request_path=request.replace('\\','/').rsplit('/',1)[0]+'/'+sheet.replace('\\','/')
        key=request_path.lower()
        if key not in _sheet_cache:_sheet_cache[key]=load_image(resources,request_path)[0]
        image=_sheet_cache[key]
        crop=(int(float(frame.get('XCrop',0))),int(float(frame.get('YCrop',0))),
              int(float(frame.get('Width',0))),int(float(frame.get('Height',0))))
        piece=image.crop((crop[0],crop[1],crop[0]+crop[2],crop[1]+crop[3]))
        tint_values=tuple(int(float(frame.get(k,'255'))) for k in ('RedTint','GreenTint','BlueTint','AlphaTint'))
        piece=tint(piece,tint_values)
        scale_x=float(frame.get('XScale','100'))/100.0;scale_y=float(frame.get('YScale','100'))/100.0
        if abs(scale_x-1.0)>1e-6 or abs(scale_y-1.0)>1e-6:
            piece=piece.resize((max(1,int(piece.width*scale_x)),max(1,int(piece.height*scale_y))),Image.BILINEAR)
        dest=(anchor[0]+int(float(frame.get('XPosition','0')))-int(float(frame.get('XPivot','0'))),
              anchor[1]+int(float(frame.get('YPosition','0')))-int(float(frame.get('YPivot','0'))))
        canvas.alpha_composite(piece,dest)
    return canvas,raw,entry,used
def animation_length(root,names):
    animations={a.get('Name'):a for a in root.find('Animations')}
    names=[choose_animation(root,name) for name in names]
    count=1
    for layer in root.find('Content/Layers'):
        for name in names:
            if not name or name not in animations:continue
            for track in animations[name].find('LayerAnimations'):
                if track.get('LayerId')==layer.get('Id'):
                    count=max(count,len(list(track)));break
    return count

def prepare():
    resources=OriginalResources(ROOT/'runtime/game')
    items=[]
    #Load every prepared image first, then shelf-pack by descending height.
    for name,request in IMAGES:
        image,raw,entry=load_image(resources,request)
        items.append(dict(name=name,image=image,request=request,raw=raw,entry=entry,animated=False))
    for name,request,primary,overlay in ACTORS:
        image,raw,entry,used=compose_actor(resources,request,primary,overlay)
        items.append(dict(name=name,image=image,request=request,raw=raw,entry=entry,animated=True,
            composed='%s%s'%(primary,'/'+overlay if overlay else ''),tracks=used))
    for name,request,primary,overlay,size in ANIMATED:
        raw,entry=resources.read(request);root=ET.fromstring(raw)
        primary=choose_animation(root,primary);frames=min(FRAME_LIMITS.get(name,ANIMATION_FRAME_LIMIT),max(1,animation_length(root,(primary,overlay))))
        strip=Image.new('RGBA',(size*frames,size),(0,0,0,0));used=[]
        for index in range(frames):
            image,_,_,used=compose_actor(resources,request,primary,overlay,index,size)
            strip.alpha_composite(image,(index*size,0))
        items.append(dict(name=name,image=strip,request=request,raw=raw,entry=entry,animated=True,
            composed='%s%s'%(primary,'/'+overlay if overlay else ''),tracks=used,frames=frames,
            frameWidth=size,frameHeight=size))
    items.sort(key=lambda it:(-it['image'].height,-it['image'].width,it['name']))
    atlas=Image.new('RGBA',(WIDTH,HEIGHT),(0,0,0,0))
    manifest={'format':'gameplay-atlas-v2','width':WIDTH,'height':HEIGHT,'entries':[]}
    #Skyline packing. Wide backdrops form a floor; the narrow strips then fill
    #the pockets above/beside them instead of wasting whole shelf rows.
    skyline=[(0,0,WIDTH)]
    for it in items:
        image=it['image'];w,h=image.width,image.height
        if w>WIDTH or h>HEIGHT:raise ValueError('Atlas entry too large: '+it['name'])
        best=None
        for sx,sy,sw in skyline:
            x=sx
            if x+w>WIDTH:continue
            y=0
            for tx,ty,tw in skyline:
                if tx<x+w and tx+tw>x and ty>y:y=ty
            if y+h<=HEIGHT and (best is None or (y,x)<(best[0],best[1])):best=(y,x)
        if best is None:raise ValueError('Gameplay atlas overflow at '+it['name'])
        y,x=best
        atlas.alpha_composite(image,(x,y))
        record=dict(name=it['name'],request=it['request'],package=it['entry']['package'],
            sha256=hashlib.sha256(it['raw']).hexdigest(),x=x,y=y,width=w,height=h)
        if it['animated']:
            record.update(composed=it['composed'],tracks=it['tracks'])
        if it.get('frames'):
            record.update(frames=it['frames'],frameWidth=it['frameWidth'],frameHeight=it['frameHeight'])
        nextline=[]
        for sx,sy,sw in skyline:
            if sx+sw<=x or sx>=x+w:nextline.append((sx,sy,sw));continue
            if sx<x:nextline.append((sx,sy,x-sx))
            if sx+sw>x+w:nextline.append((x+w,sy,sx+sw-(x+w)))
        nextline.append((x,y+h,w));nextline.sort()
        merged=[]
        for seg in nextline:
            if merged and merged[-1][1]==seg[1] and merged[-1][0]+merged[-1][2]==seg[0]:
                merged[-1]=(merged[-1][0],seg[1],merged[-1][2]+seg[2])
            else:merged.append(seg)
        skyline=merged
        manifest['entries'].append(record)
    output=ROOT/'runtime/port/gameplay'
    output.mkdir(parents=True,exist_ok=True)
    with (output/'atlas.bin').open('wb') as f:
        f.write(b'GPLAY1\0\0');f.write(struct.pack('<II',WIDTH,HEIGHT))
        f.write(atlas.tobytes())
    (output/'manifest.json').write_text(json.dumps(manifest,indent=1)+'\n')
    lines=['//Prepared original gameplay atlas rectangles (linear RGBA atlas).',
           'static const unsigned int kGameplayAtlasWidth=%dU;'%WIDTH,
           'static const unsigned int kGameplayAtlasHeight=%dU;'%HEIGHT]
    for entry in manifest['entries']:
        name=entry['name'].title()
        lines.append('static const int kGameplay%sX=%d,kGameplay%sY=%d,kGameplay%sW=%d,kGameplay%sH=%d;'%(
            name,entry['x'],name,entry['y'],name,entry['width'],name,entry['height']))
        if entry.get('frames'):
            lines.append('static const int kGameplay%sFrames=%d,kGameplay%sFrameW=%d,kGameplay%sFrameH=%d;'%(
                name,entry['frames'],name,entry['frameWidth'],name,entry['frameHeight']))
    enemy=[]
    for entry in manifest['entries']:
        key=ENEMY_SPRITE_KEYS.get(entry['name'])
        if key:enemy.append((key[0],key[1],entry['x'],entry['y'],entry['frames'],entry['frameWidth'],entry['frameHeight']))
    enemy.sort()
    lines.append('struct GameplayEnemySprite{int type,variant,x,y,frames,frameW,frameH;};')
    lines.append('static const GameplayEnemySprite kGameplayEnemySprites[]={')
    for row in enemy:lines.append('{%d,%d,%d,%d,%d,%d,%d},'%row)
    lines.append('};')
    (ROOT/'build/generated/gameplay_atlas_data.h').write_text('\n'.join(lines)+'\n')
    print('Prepared gameplay atlas: %d entries, %dx%d'%(len(manifest['entries']),WIDTH,HEIGHT))

if __name__=='__main__':prepare()

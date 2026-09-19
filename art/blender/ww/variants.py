"""Enemy roster. Every entry shares the cowboy skeleton, so animations are interchangeable."""

VARIANTS = {
    "Bandit": dict(
        skin="skin_tan", hair="hair_black", shirt="blue_faded", pants="brown", boots="leather_dark",
        hat=dict(color="brown_dark", band="red", curl=0.055), mask="red", outer="vest", outer_color="brown_dark",
        weapon="revolver", spurs=True),
    "Gunslinger": dict(
        skin="skin_light", hair="hair_black", hair_style="long", beard="handlebar", shirt="cream", sleeve="charcoal",
        pants="charcoal", boots="black", gloves="black", bandana="maroon",
        hat=dict(color="black", band="steel", crown_h=0.105, top=0.95, pinch=0.0, dent=0.012, curl=0.012, dip=0.0,
                 brim=0.125, tilt=-10),
        outer="duster", outer_color="charcoal", weapon="dual", long_barrel=True, holsters=("l", "r"), spurs=True),
    "Rifleman": dict(
        skin="skin_dark", hair="hair_black", beard="full", shirt="red_faded", sleeves="rolled", pants="blue_dark",
        boots="leather", bandana="cream", bandolier="ammo",
        hat=dict(color="tan", band="leather_dark", curl=0.030, brim=0.130, crown_h=0.115, tilt=-4),
        weapon="rifle"),
    "Deputy": dict(
        skin="skin_light", hair="hair_ginger", beard="mustache", shirt="white", pants="tan", boots="brown_dark",
        bandana="blue_dark", outer="vest", outer_color="blue_dark", badge=True,
        hat=dict(color="cream", band="brown", crown_h=0.145, top=0.78, curl=0.040, brim=0.105),
        weapon="revolver"),
    "Dynamiter": dict(
        skin="skin_red", hair="hair_ginger", beard="big", shirt="olive", sleeves="rolled", pants="brown_dark",
        boots="black", bandolier="dynamite", gloves="leather_dark",
        hat=dict(color="charcoal", band="mustard", round=True, crown_h=0.115, brim=0.050, curl=0.018, dip=0.0,
                 tilt=-3),
        weapon="dynamite"),
    "Heavy": dict(
        skin="skin_tan", hair="hair_brown", hair_style="bald", beard="big", shirt="maroon", sleeves="rolled",
        pants="charcoal", boots="brown_dark", outer="vest", outer_color="leather", bandana="mustard",
        girth=1.32, scale=1.10, eyepatch=True,
        hat=dict(color="black", band="maroon", round=True, crown_h=0.105, brim=0.045, curl=0.015, dip=0.0, tilt=-2),
        weapon="shotgun"),
    "Boss": dict(
        skin="skin_tan", hair="hair_brown", hair_style="long", beard="full", shirt="cream", sleeve="maroon",
        pants="plum", boots="brown_dark", gloves="leather_dark", kneepads="grey", cigar=True, spurs=True,
        outer="poncho", outer_color="red", outer_trim="cream", scale=1.06, girth=1.08,
        hat=dict(color="maroon", band="black", brim=0.150, curl=0.085, dip=0.030, crown_h=0.135, buckle=True,
                 tilt=-9),
        weapon="revolver", long_barrel=True),
}

HORSES = {
    "Horse_Bay": dict(coat="horse_bay", mane="mane_black", sock="horse_black", blanket="red", saddle="leather"),
    "Horse_Black": dict(coat="horse_black", mane="mane_black", sock=None, blanket="mustard", saddle="leather_dark"),
    "Horse_Palomino": dict(coat="horse_palomino", mane="mane_cream", sock="horse_white", blanket="blue_dark",
                           saddle="leather_light", blaze=True),
    "Horse_Grey": dict(coat="horse_grey", mane="mane_brown", sock=None, blanket="maroon", saddle="leather",
                       blaze=True),
}

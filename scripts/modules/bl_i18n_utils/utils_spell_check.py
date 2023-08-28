# SPDX-FileCopyrightText: 2012-2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

import enchant
import os
import pickle
import re


class SpellChecker:
    """
    A basic spell checker.
    """

    # These must be all lower case for comparisons
    uimsgs = {
        # OK words
        "adaptively", "adaptivity",
        "al",  # et al.
        "aren",  # aren't
        "betweens",  # yuck! in-betweens!
        "boolean", "booleans",
        "chamfer",
        "couldn",  # couldn't
        "customizable",
        "decrement",
        "derivate",
        "deterministically",
        "doesn",  # doesn't
        "duplications",
        "effector",
        "equi",  # equi-angular, etc.
        "et",  # et al.
        "fader",
        "globbing",
        "gridded",
        "haptics",
        "hasn",  # hasn't
        "hetero",
        "hoc",  # ad-hoc
        "incompressible",
        "indices",
        "instantiation",
        "iridas",
        "isn",  # isn't
        "iterable",
        "kyrgyz",
        "latin",
        "merchantability",
        "mplayer",
        "ons",  # add-ons
        "pong",  # ping pong
        "procedurals",  # Used as noun
        "resumable",
        "runtimes",
        "scalable",
        "shadeless",
        "shouldn",  # shouldn't
        "smoothen",
        "spacings",
        "teleport", "teleporting",
        "tangency",
        "vertices",
        "wasn",  # wasn't
        "zig", "zag",

        # Brands etc.
        "htc",
        "huawei",
        "radeon",
        "vive",
        "xbox",

        # Merged words
        "antialiasing", "antialias",
        "arcsine", "arccosine", "arctangent",
        "autoclip",
        "autocomplete",
        "autoexec",
        "autoexecution",
        "autogenerated",
        "autolock",
        "automask", "automasking",
        "automerge",
        "autoname",
        "autopack",
        "autosave",
        "autoscale",
        "autosmooth",
        "autosplit",
        "backend", "backends",
        "backface", "backfacing",
        "backimage",
        "backscattered",
        "bandnoise",
        "bindcode",
        "bitdepth",
        "bitflag", "bitflags",
        "bitrate",
        "blackbody",
        "blendfile",
        "blendin",
        "bonesize",
        "boundbox",
        "boxpack",
        "buffersize",
        "builtin", "builtins",
        "bytecode",
        "chunksize",
        "codebase",
        "customdata",
        "dataset", "datasets",
        "de",
        "deadzone",
        "decomposable",
        "deconstruct",
        "defocus",
        "denoise", "denoised", "denoising", "denoiser",
        "deselect", "deselecting", "deselection",
        "despill", "despilling",
        "dirtree",
        "editcurve",
        "editmesh",
        "faceforward",
        "filebrowser",
        "filelist",
        "filename", "filenames",
        "filepath", "filepaths",
        "forcefield", "forcefields",
        "framerange",
        "frontmost",
        "fulldome", "fulldomes",
        "fullscreen",
        "gamepad",
        "gridline", "gridlines",
        "hardlight",
        "hemi",
        "hostname",
        "inbetween",
        "inscatter", "inscattering",
        "libdata",
        "lightcache",
        "lightgroup", "lightgroups",
        "lightprobe", "lightprobes",
        "lightless",
        "lineset",
        "linestyle", "linestyles",
        "localview",
        "lookup", "lookups",
        "mathutils",
        "micropolygon",
        "midlevel",
        "midground",
        "mixdown",
        "monospaced",
        "multi",
        "multifractal",
        "multiframe",
        "multilayer",
        "multipaint",
        "multires", "multiresolution",
        "multisampling",
        "multiscatter",
        "multitexture",
        "multithreaded",
        "multiuser",
        "multiview",
        "namespace",
        "nodetree", "nodetrees",
        "keyconfig",
        "offscreen",
        "online",
        "playhead",
        "popup", "popups",
        "pointcloud",
        "pre",
        "precache", "precaching",
        "precalculate",
        "precomputing",
        "prefetch",
        "prefilter", "prefiltering",
        "preload",
        "premultiply", "premultiplied",
        "prepass",
        "prepend",
        "preprocess", "preprocessing", "preprocessor", "preprocessed",
        "preseek",
        "preselect", "preselected",
        "promillage",
        "pushdown",
        "raytree",
        "readonly",
        "realtime",
        "reinject", "reinjected",
        "rekey",
        "relink",
        "remesh",
        "reprojection", "reproject", "reprojecting",
        "resample",
        "rescale",
        "resize",
        "restpose",
        "resync", "resynced",
        "retarget", "retargets", "retargeting", "retargeted",
        "retime", "retimed", "retiming",
        "rigidbody",
        "ringnoise",
        "rolloff",
        "runtime",
        "scanline",
        "screenshot", "screenshots",
        "seekability",
        "selfcollision",
        "shadowbuffer", "shadowbuffers",
        "singletexture",
        "softbox",
        "spellcheck", "spellchecking",
        "startup",
        "stateful",
        "starfield",
        "studiolight",
        "subflare", "subflares",
        "subframe", "subframes",
        "subclass", "subclasses", "subclassing",
        "subdirectory", "subdirectories", "subdir", "subdirs",
        "subitem",
        "submode",
        "submodule", "submodules",
        "subpath",
        "subsample", "subsamples", "subsampling",
        "subsize",
        "substep", "substeps",
        "substring",
        "targetless",
        "textbox", "textboxes",
        "tilemode",
        "timestamp", "timestamps",
        "timestep", "timesteps",
        "todo",
        "tradeoff",
        "un",
        "unadjust", "unadjusted",
        "unassociate", "unassociated",
        "unbake",
        "uncheck",
        "unclosed",
        "uncomment",
        "unculled",
        "undeformed",
        "undistort", "undistorted", "undistortion",
        "ungroup", "ungrouped",
        "unhandled",
        "unhide",
        "unindent",
        "unitless",
        "unkeyed",
        "unlink", "unlinked",
        "unmute",
        "unphysical",
        "unpremultiply",
        "unprojected",
        "unprotect",
        "unreacted",
        "unreferenced",
        "unregister", "unregistration",
        "unselect", "unselected", "unselectable",
        "unsets",
        "unshadowed",
        "unspill",
        "unstitchable", "unstitch",
        "unsubdivided", "unsubdivide",
        "untrusted",
        "vectorscope",
        "whitespace", "whitespaces",
        "worldspace",
        "workflow",
        "workspace", "workspaces",

        # Neologisms, slangs
        "affectable",
        "animatable",
        "automagic", "automagically",
        "blobby",
        "blockiness", "blocky",
        "collider", "colliders",
        "deformer", "deformers",
        "determinator",
        "editability",
        "effectors",
        "expander",
        "instancer",
        "keyer",
        "lacunarity",
        "linkable",
        "numerics",
        "occluder", "occluders",
        "overridable",
        "passepartout",
        "perspectively",
        "pixelate",
        "pointiness",
        "polycount",
        "polygonization", "polygonalization",  # yuck!
        "scalings",
        "selectable", "selectability",
        "shaper",
        "smoothen", "smoothening",
        "spherize", "spherized",
        "statting",  # Running `stat` command, yuck!
        "stitchable",
        "symmetrize",
        "trackability",
        "transmissivity",
        "rasterized", "rasterization", "rasterizer",
        "renderer", "renderers", "renderable", "renderability",

        # Really bad!!!
        "convertor",
        "fullscr",

        # Abbreviations
        "aero",
        "amb",
        "anim",
        "aov",
        "app",
        "args",  # Arguments
        "bbox", "bboxes",
        "bksp",  # Backspace
        "bool",
        "calc",
        "cfl",
        "config", "configs",
        "const",
        "coord", "coords",
        "degr",
        "diff",
        "dof",
        "dupli", "duplis",
        "eg",
        "esc",
        "expr",
        "fac",
        "fra",
        "fract",
        "frs",
        "grless",
        "http",
        "init",
        "irr",  # Irradiance
        "kbit", "kb",
        "lang", "langs",
        "lclick", "rclick",
        "lensdist",
        "loc", "rot", "pos",
        "lorem",
        "luma",
        "mbs",  # mouse button 'select'.
        "mem",
        "mul",  # Multiplicative etc.
        "multicam",
        "num",
        "ok",
        "orco",
        "ortho",
        "pano",
        "persp",
        "pref", "prefs",
        "prev",
        "param",
        "premul",
        "quad", "quads",
        "quat", "quats",
        "recalc", "recalcs",
        "refl",
        "sce",
        "sel",
        "spec",
        "struct", "structs",
        "subdiv",
        "sys",
        "tex",
        "texcoord",
        "tmr",  # timer
        "tri", "tris",
        "udim", "udims",
        "upres",  # Upresolution
        "usd",
        "uv", "uvs", "uvw", "uw", "uvmap",
        "ve",
        "vec",
        "vel",  # velocity!
        "vert", "verts",
        "vis",
        "vram",
        "xor",
        "xyz", "xzy", "yxz", "yzx", "zxy", "zyx",
        "xy", "xz", "yx", "yz", "zx", "zy",

        # General computer/science terms
        "affine",
        "albedo",
        "anamorphic",
        "anisotropic", "anisotropy",
        "arcminute", "arcminutes",
        "arcsecond", "arcseconds",
        "bimanual",  # OpenXR?
        "bitangent",
        "boid", "boids",
        "ceil",
        "centum",  # From 'centum weight'
        "compressibility",
        "coplanar",
        "curvilinear",
        "dekameter", "dekameters",
        "equiangular",
        "equisolid",
        "euler", "eulers",
        "eumelanin",
        "fribidi",
        "gettext",
        "hashable",
        "hotspot",
        "hydrostatic",
        "interocular",
        "intrinsics",
        "irradiance",
        "isosurface",
        "jitter", "jittering", "jittered",
        "keymap", "keymaps",
        "lambertian",
        "laplacian",
        "metadata",
        "microwatt", "microwatts",
        "microflake",
        "milliwatt", "milliwatts",
        "msgfmt",
        "nand", "xnor",
        "nanowatt", "nanowatts",
        "normals",
        "numpad",
        "octahedral",
        "octree",
        "omnidirectional",
        "opengl",
        "openmp",
        "parametrization",
        "pheomelanin",
        "photoreceptor",
        "poly",
        "polyline", "polylines",
        "probabilistically",
        "pulldown", "pulldowns",
        "quadratically",
        "quantized",
        "quartic",
        "quaternion", "quaternions",
        "quintic",
        "samplerate",
        "sandboxed",
        "sawtooth",
        "scrollback",
        "scrollbar",
        "scroller",
        "searchable",
        "spacebar",
        "subtractive",
        "superellipse",
        "thumbstick",
        "tooltip", "tooltips",
        "touchpad", "trackpad",
        "trilinear",
        "triquadratic",
        "tuple",
        "unicode",
        "viewport", "viewports",
        "viscoelastic",
        "vorticity",
        "waveform", "waveforms",
        "wildcard", "wildcards",
        "wintab",  # Some Windows tablet API

        # General computer graphics terms
        "anaglyph",
        "bezier", "beziers",
        "bicubic",
        "bilinear",
        "bindpose",
        "binormal",
        "blackpoint", "whitepoint",
        "blendshape", "blendshapes",  # USD slang :(
        "blinn",
        "bokeh",
        "catadioptric",
        "centroid",
        "chroma",
        "chrominance",
        "clearcoat",
        "codec", "codecs",
        "collada",
        "colorspace",
        "compositing",
        "crossfade",
        "cubemap", "cubemaps",
        "cuda",
        "deinterlace",
        "dropoff",
        "duotone",
        "dv",
        "eigenvectors",
        "emissive",
        "equirectangular",
        "fader",
        "filmlike",
        "fisheye",
        "framerate",
        "gimbal",
        "grayscale",
        "icosahedron",
        "icosphere",
        "illuminant",  # CIE illuminant D65
        "inpaint",
        "kerning",
        "lightmap",
        "linearlight",
        "lossless", "lossy",
        "luminance",
        "mantaflow",
        "matcap",
        "microfacet",
        "midtones",
        "mipmap", "mipmaps", "mip",
        "ngon", "ngons",
        "ntsc",
        "nurb", "nurbs",
        "perlin",
        "phong",
        "photorealistic",
        "pinlight",
        "posterize",
        "primvar", "primvars",  # USD slang :(
        "qi",
        "radiosity",
        "raycast", "raycasting",
        "raymarching",
        "raytrace", "raytracing", "raytraced",
        "refractions",
        "remesher", "remeshing", "remesh",
        "renderfarm",
        "retopology",
        "scanfill",
        "shader", "shaders",
        "shadowmap", "shadowmaps",
        "softlight",
        "specular", "specularity",
        "spillmap",
        "sobel",
        "stereoscopy",
        "surfel", "surfels",  # Surface Element
        "texel",
        "timecode",
        "tonemap",
        "toon",
        "transmissive",
        "uvproject",
        "vividlight",
        "volumetrics",
        "voronoi",
        "voxel", "voxels",
        "vsync",
        "vulkan",
        "wireframe", "wireframes",
        "xforms",  # USD slang :(
        "zmask",
        "ztransp",

        # Blender terms
        "audaspace",
        "azone",  # action zone
        "backwire",
        "bbone",
        "bdata",
        "bendy",  # bones
        "bmesh",
        "breakdowner",
        "bspline",
        "bweight",
        "colorband",
        "crazyspace",
        "datablock", "datablocks",
        "despeckle",
        "depsgraph",
        "dopesheet",
        "dupliface", "duplifaces",
        "dupliframe", "dupliframes",
        "dupliobject", "dupliob",
        "dupligroup",
        "duplivert",
        "dyntopo",
        "editbone",
        "editmode",
        "eevee",
        "fcurve", "fcurves",
        "fedge", "fedges",
        "filmic",
        "fluidsim",
        "freestyle",
        "enum", "enums",
        "gizmogroup",
        "gon", "gons",  # N-Gon(s)
        "gpencil",
        "idcol",
        "keyframe", "keyframes", "keyframing", "keyframed",
        "lookdev",
        "luminocity",
        "mathvis",
        "metaball", "metaballs", "mball",
        "metaelement", "metaelements",
        "metastrip", "metastrips",
        "movieclip",
        "mpoly",
        "mtex",
        "nabla",
        "navmesh",
        "outliner",
        "overscan",
        "paintmap", "paintmaps",
        "polygroup", "polygroups",
        "poselib",
        "pushpull",
        "pyconstraint", "pyconstraints",
        "qe",  # keys...
        "shaderfx", "shaderfxs",
        "shapekey", "shapekeys",
        "shrinkfatten",
        "shrinkwrap",
        "softbody",
        "stucci",
        "subdiv",
        "subtype",
        "sunsky",
        "tessface", "tessfaces",
        "texface",
        "timeline", "timelines",
        "tmpact",  # sigh...
        "tosphere",
        "uilist",
        "userpref",
        "vcol", "vcols",
        "vgroup", "vgroups",
        "vinterlace",
        "vse",
        "wasd", "wasdqe",  # keys...
        "wetmap", "wetmaps",
        "wpaint",
        "uvwarp",

        # UOC (Ugly Operator Categories)
        "cachefile",
        "paintcurve",
        "ptcache",
        "dpaint",

        # Algorithm/library names
        "ashikhmin",  # Ashikhmin-Shirley
        "arsloe",  # Texel-Marsen-Arsloe
        "beckmann",
        "blackman",  # Blackman-Harris
        "blosc",
        "burley",  # Christensen-Burley
        "butterworth",
        "catmull",
        "catrom",
        "chiang",
        "chebychev",
        "conrady",  # Brown-Conrady
        "courant",
        "cryptomatte", "crypto",
        "devlin",
        "embree",
        "gmp",
        "hosek",
        "kutta",
        "kuwahara",
        "lennard",
        "marsen",  # Texel-Marsen-Arsloe
        "mikktspace",
        "minkowski",
        "minnaert",
        "mises",  # von Mises-Fisher
        "moskowitz",  # Pierson-Moskowitz
        "musgrave",
        "nayar",
        "netravali",
        "nishita",
        "ogawa",
        "oren",
        "peucker",  # Ramer-Douglas-Peucker
        "pierson",  # Pierson-Moskowitz
        "preetham",
        "prewitt",
        "ramer",  # Ramer-Douglas-Peucker
        "reinhard",
        "runge",
        "sobol",
        "verlet",
        "von",  # von Mises-Fisher
        "wilkie",
        "worley",

        # Acronyms
        "aa", "msaa",
        "acescg",  # ACEScg color space.
        "ao",
        "aov", "aovs",
        "api",
        "apic",  # Affine Particle-In-Cell
        "asc", "cdl",
        "ascii",
        "atrac",
        "avx",
        "bsdf", "bsdfs",
        "bssrdf",
        "bt",  #BT.1886 2.4 Exponent EOTF
        "bw",
        "ccd",
        "cie",  # CIE XYZ color space
        "cmd",
        "cmos",
        "cpus",
        "ctrl",
        "cw", "ccw",
        "dci",  # DCI-P3 D65
        "dev",
        "dls",
        "djv",
        "dpi",
        "dvar",
        "dx",
        "eo",
        "eotf",  #BT.1886 2.4 Exponent EOTF
        "ewa",
        "fh",
        "fk",
        "fov",
        "fft",
        "futura",
        "fx",
        "gfx",
        "ggx",
        "gl",
        "glsl",
        "gpl",
        "gpu", "gpus",
        "hc",
        "hdc",
        "hdr", "hdri", "hdris",
        "hh", "mm", "ss", "ff",  # hh:mm:ss:ff timecode
        "hpg",  # Intel Xe-HPG architecture
        "hsv", "hsva", "hsl",
        "id",
        "iec",  # sRGB IEC 61966-2-1
        "ies",
        "ior",
        "itu",
        "jonswap",
        "lfe",
        "lhs",
        "lmb", "mmb", "rmb",
        "lscm",
        "lx",  # Lux light unit
        "kb",
        "mis",
        "mocap",
        "msgid", "msgids",
        "mux",
        "ndof",
        "pbr",  # Physically Based Rendering
        "ppc",
        "precisa",
        "px",
        "qmc",
        "rdna",
        "rdp",
        "rgb", "rgba",
        "ris",
        "rhs",
        "rpp",  # Eevee ray-tracing?
        "rv",
        "sdf",
        "sdl",
        "sdls",
        "sl",
        "smpte",
        "ssao",
        "ssr",
        "svn",
        "tma",
        "ui",
        "unix",
        "uuid",
        "vbo", "vbos",
        "vfx",
        "vmm",
        "vr",
        "wxyz",
        "xform",
        "xr",
        "ycc", "ycca",
        "yrgb",
        "yuv", "yuva",

        # Blender acronyms
        "bge",
        "bli",
        "bpy",
        "bvh",
        "dbvt",
        "dop",  # BLI K-Dop BVH
        "ik",
        "nla",
        "py",
        "qbvh",
        "rna",
        "rvo",
        "simd",
        "sph",
        "svbvh",

        # Files types/formats
        "aac",
        "avi",
        "attrac",
        "autocad",
        "autodesk",
        "bmp",
        "btx",
        "cineon",
        "dpx",
        "dwaa",
        "dwab",
        "dxf",
        "eps",
        "exr",
        "fbx",
        "fbxnode",
        "ffmpeg",
        "flac",
        "gltf",
        "gprim",  # From USD.
        "gzip",
        "ico",
        "jpg", "jpeg", "jpegs",
        "json",
        "lightwave",
        "lzw",
        "matroska",
        "mdd",
        "mkv",
        "mpeg", "mjpeg",
        "mtl",
        "ogg",
        "openjpeg",
        "osl",
        "oso",
        "pcm",
        "piz",
        "png", "pngs",
        "po",
        "quicktime",
        "rle",
        "sgi",
        "stl",
        "svg",
        "targa", "tga",
        "tiff",
        "theora",
        "usdz",
        "vdb",
        "vorbis",
        "vp9",
        "wav",
        "webm",
        "xiph",
        "xml",
        "xna",
        "xvid",
    }

    _valid_before = "(?<=[\\s*'\"`])|(?<=[a-zA-Z][/-])|(?<=^)"
    _valid_after = "(?=[\\s'\"`.!?,;:])|(?=[/-]\\s*[a-zA-Z])|(?=$)"
    _valid_words = "(?:{})(?:(?:[A-Z]+[a-z]*)|[A-Z]*|[a-z]*)(?:{})".format(_valid_before, _valid_after)
    _split_words = re.compile(_valid_words).findall

    @classmethod
    def split_words(cls, text):
        return [w for w in cls._split_words(text) if w]

    def __init__(self, settings, lang="en_US"):
        self.settings = settings
        self.dict_spelling = enchant.Dict(lang)
        self.cache = set(self.uimsgs)

        cache = self.settings.SPELL_CACHE
        if cache and os.path.exists(cache):
            with open(cache, 'rb') as f:
                self.cache |= set(pickle.load(f))

    def __del__(self):
        cache = self.settings.SPELL_CACHE
        if cache and os.path.exists(cache):
            with open(cache, 'wb') as f:
                pickle.dump(self.cache, f)

    def check(self, txt):
        ret = []

        if txt in self.cache:
            return ret

        for w in self.split_words(txt):
            w_lower = w.lower()
            if w_lower in self.cache:
                continue
            if not self.dict_spelling.check(w):
                ret.append((w, self.dict_spelling.suggest(w)))
            else:
                self.cache.add(w_lower)

        if not ret:
            self.cache.add(txt)

        return ret

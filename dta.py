# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild
# type: ignore

import kaitaistruct
from kaitaistruct import KaitaiStruct, KaitaiStream, BytesIO


if getattr(kaitaistruct, 'API_VERSION', (0, 9)) < (0, 11):
    raise Exception("Incompatible Kaitai Struct Python API: 0.11 or later is required, but you have %s" % (kaitaistruct.__version__))

class Dta(KaitaiStruct):
    """Animation data file format for LEGO Island (1997). Contains animation
    information for world objects including their positions, orientations,
    and associated models.
    
    DTA files are located at `<install_path>/lego/data/<world>inf.dta` where
    <world> is the world name (e.g., "isle", "act1", "act2m", etc.). They are
    loaded by LegoAnimationManager::LoadWorldInfo() to populate animation
    information for the current world.
    
    File structure:
    1. Header - version (must be 3) and animation count
    2. AnimInfo entries - animation references with nested model placement data
    """
    def __init__(self, _io, _parent=None, _root=None):
        super(Dta, self).__init__(_io)
        self._parent = _parent
        self._root = _root or self
        self._read()

    def _read(self):
        self.version = self._io.read_u4le()
        self.num_anims = self._io.read_u2le()
        self.anims = []
        for i in range(self.num_anims):
            self.anims.append(Dta.AnimInfo(self._io, self, self._root))



    def _fetch_instances(self):
        pass
        for i in range(len(self.anims)):
            pass
            self.anims[i]._fetch_instances()


    class AnimInfo(KaitaiStruct):
        """Animation information for a single animation (AnimInfo struct).
        Contains metadata about the animation and a list of models involved.
        Parsed by LegoAnimationManager::ReadAnimInfo().
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(Dta.AnimInfo, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.name_length = self._io.read_u1()
            self.name = (self._io.read_bytes(self.name_length)).decode(u"ASCII")
            self.object_id = self._io.read_u4le()
            self.location = self._io.read_s2le()
            self.unk_0x0a = self._io.read_u1()
            self.unk_0x0b = self._io.read_u1()
            self.unk_0x0c = self._io.read_u1()
            self.unk_0x0d = self._io.read_u1()
            self.unk_0x10 = []
            for i in range(4):
                self.unk_0x10.append(self._io.read_f4le())

            self.model_count = self._io.read_u1()
            self.models = []
            for i in range(self.model_count):
                self.models.append(Dta.ModelInfo(self._io, self, self._root))



        def _fetch_instances(self):
            pass
            for i in range(len(self.unk_0x10)):
                pass

            for i in range(len(self.models)):
                pass
                self.models[i]._fetch_instances()



    class ModelInfo(KaitaiStruct):
        """Model information defining position and orientation for a single
        model within an animation (ModelInfo struct). Used to place characters
        and objects in the world during animation playback.
        Parsed by LegoAnimationManager::ReadModelInfo().
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(Dta.ModelInfo, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.name_length = self._io.read_u1()
            self.name = (self._io.read_bytes(self.name_length)).decode(u"ASCII")
            self.unk_0x04 = self._io.read_u1()
            self.position = Dta.Vertex3(self._io, self, self._root)
            self.direction = Dta.Vertex3(self._io, self, self._root)
            self.up = Dta.Vertex3(self._io, self, self._root)
            self.unk_0x2c = self._io.read_u1()


        def _fetch_instances(self):
            pass
            self.position._fetch_instances()
            self.direction._fetch_instances()
            self.up._fetch_instances()


    class Vertex3(KaitaiStruct):
        """A 3D point or vector with X, Y, Z components."""
        def __init__(self, _io, _parent=None, _root=None):
            super(Dta.Vertex3, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.x = self._io.read_f4le()
            self.y = self._io.read_f4le()
            self.z = self._io.read_f4le()


        def _fetch_instances(self):
            pass




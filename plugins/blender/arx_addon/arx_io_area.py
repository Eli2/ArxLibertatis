# Copyright 2019-2020 Arx Libertatis Team (see the AUTHORS file)
#
# This file is part of Arx Libertatis.
#
# Arx Libertatis is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Arx Libertatis is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Arx Libertatis. If not, see <http://www.gnu.org/licenses/>.

import logging

import os
from pathlib import Path

import bpy
import bmesh
from math import (
    degrees,
    radians
)
from mathutils import Vector, Matrix

from .dataCommon import (
    SavedVec3,
    SavedAnglef
)
from .dataDlf import DlfSerializer, DlfData, DANAE_LS_INTER
from .dataFts import FtsSerializer
from .dataLlf import LlfSerializer

from .arx_io_material import createMaterial
from .arx_io_util import arx_pos_to_blender_for_model, blender_pos_to_arx, ArxException

correctionMatrix = \
    Matrix.Rotation(radians(180), 4, 'Z') @ \
    Matrix.Rotation(radians(-90), 4, 'X')

def getObjectPosition(object, position: SavedVec3):
    p = blender_pos_to_arx(object.location)
    position.x = p[0]
    position.y = p[1]
    position.z = p[2]

def setObjectPosition(object, offset, position: SavedVec3):
    p = Vector(offset) + Vector([position.x, position.y, position.z])
    object.location = arx_pos_to_blender_for_model(p)

def getObjectRotation(object, rotation: SavedAnglef):
    # FIXME proper rotation conversion
    rotation.a = degrees(object.rotation_euler[0])
    rotation.b = degrees(object.rotation_euler[1])
    rotation.g = degrees(object.rotation_euler[2])

def setObjectRotation(object, rotation: SavedAnglef):
    # FIXME proper rotation conversion
    object.rotation_mode = 'YXZ'
    object.rotation_euler = [radians(rotation.a), radians(rotation.g), radians(rotation.b)]


class ArxSceneManager(object):
    def __init__(self, ioLib, dataPath, outPath, arxFiles, objectManager):
        self.log = logging.getLogger('ArxSceneManager')
        self.dlfSerializer = DlfSerializer(ioLib)
        self.ftsSerializer = FtsSerializer(ioLib)
        self.llfSerializer = LlfSerializer(ioLib)
        self.dataPath = dataPath
        self.outPath = outPath
        self.arxFiles = arxFiles
        self.objectManager = objectManager

    def importScene(self, context, scene, area_id):
        self.log.info('Importing Area: {}'.format(area_id))
        
        area_files = self.arxFiles.levels.levels[area_id]
        
        if area_files.dlf is None:
            self.log.error("dlf file not found")
            return
        if area_files.fts is None:
            self.log.error("fts file not found")
            return
        if area_files.llf is None:
            self.log.error("llf file not found")
            return
        
        dlfData = self.dlfSerializer.readContainer(area_files.dlf)
        ftsData = self.ftsSerializer.read_fts_container(area_files.fts)
        llfData = self.llfSerializer.read(area_files.llf)

        # bpy.types.Material.Shader_Name = bpy.props.StringProperty(name='Group Name')

        # Create materials
        mappedMaterials = []
        idx = 0
        for tex in ftsData.textures:
            mappedMaterials.append((idx, tex.tc, createMaterial(self.dataPath, tex.fic.decode('iso-8859-1'))))
            idx += 1

        # Create mesh
        bm = self.AddSceneBackground(ftsData.cells, llfData.levelLighting, mappedMaterials)
        mesh = bpy.data.meshes.new(scene.name + "-mesh")
        bm.to_mesh(mesh)
        bm.free()

        # Create background object
        obj = bpy.data.objects.new(scene.name + "-background", mesh)
        scene.collection.objects.link(obj)
        # scn.objects.active = obj
        # obj.select = True

        # Create materials
        for idx, tcId, mat in mappedMaterials:
            obj.data.materials.append(mat)

        self.AddScenePathfinderAnchors(scene, ftsData.anchors)
        self.AddScenePortals(scene, ftsData)
        self.AddSceneLights(scene, llfData, ftsData.sceneOffset)
        self.AddSceneObjects(scene, dlfData, ftsData.sceneOffset)
        self.AddPaths(scene, dlfData, ftsData.sceneOffset)
        self.AddPlayer(scene, dlfData, ftsData.sceneOffset)
        self.add_scene_map_camera(scene)

    def exportArea(self, context, scene, area_id):
        self.log.info('Exporting Area: {}'.format(area_id))

        area_files = self.arxFiles.levels.levels[area_id]

        relDlf = os.path.relpath(area_files.dlf, self.arxFiles.rootPath)
        relFts = os.path.relpath(area_files.fts, self.arxFiles.rootPath)
        relLlf = os.path.relpath(area_files.llf, self.arxFiles.rootPath)

        outDlf = Path(self.outPath, relDlf)
        outFts = Path(self.outPath, relFts)
        outLlf = Path(self.outPath, relLlf)

        print('{} {} {}'.format(outDlf, outFts, outLlf))
        
        #dlfFile = io.BytesIO
        outDlf.parent.mkdir(parents=True, exist_ok=True)
        dlfFile = open(outDlf, "wb")
        dlfData = DlfData(area_id, SavedVec3(), SavedAnglef(), [], [], [])
        self.GetPlayer(scene, dlfData)
        self.getSceneObjects(dlfData, scene)
        self.dlfSerializer.writeContainer(dlfFile, dlfData)

    def AddSceneBackground(self, cells, levelLighting, mappedMaterials):
        bm = bmesh.new()
        uvLayer = bm.loops.layers.uv.verify()
        colorLayer = bm.loops.layers.color.new("light-color")

        


        vertexIndex = 0
        for z in cells:
            for cell in z:

                if cell is None:
                    continue

                for face in cell:

                    if face.type.POLY_QUAD:
                        to = 4
                    else:
                        to = 3

                    tempVerts = []
                    for i in range(to):
                        pos = [face.v[i].ssx, face.v[i].sy, face.v[i].ssz]
                        uv = [face.v[i].stu, 1 - face.v[i].stv]
                        intCol = levelLighting[vertexIndex]
                        floatCol = (intCol.r / 255.0, intCol.g / 255.0, intCol.b / 255.0, intCol.a / 255.0)
                        tempVerts.append((pos, uv, floatCol))
                        vertexIndex += 1

                    # Switch the vertex order
                    if face.type.POLY_QUAD:
                        tempVerts[2], tempVerts[3] = tempVerts[3], tempVerts[2]

                    vertIdx = []
                    for i in tempVerts:
                        vertIdx.append(bm.verts.new(arx_pos_to_blender_for_model(i[0])))

                    bmFace = bm.faces.new(vertIdx)

                    if face.tex != 0:
                        matIdx = next((x for x in mappedMaterials if x[1] == face.tex), None)

                        if matIdx is not None:
                            bmFace.material_index = matIdx[0]
                        else:
                            self.log.info("Matrial id not found %i" % face.tex)

                    for i, loop in enumerate(bmFace.loops):
                        loop[uvLayer].uv = tempVerts[i][1]
                        loop[colorLayer] = tempVerts[i][2]

        bm.verts.index_update()
        bm.edges.index_update()
        #bm.transform(correctionMatrix)
        return bm
    
    def AddScenePathfinderAnchors(self, scene, anchors):
        
        bm = bmesh.new()
        
        bVerts = []
        for anchor in anchors:
            bVerts.append(bm.verts.new(arx_pos_to_blender_for_model(anchor[0])))
        
        bm.verts.index_update()
        
        for i, anchor in enumerate(anchors):
            for edge in anchor[1]:
                #TODO this is a hack
                try:
                    bm.edges.new((bVerts[i], bVerts[edge]));
                except ValueError:
                    pass
        
        #bm.transform(correctionMatrix)
        mesh = bpy.data.meshes.new(scene.name + '-anchors-mesh')
        bm.to_mesh(mesh)
        bm.free()
        obj = bpy.data.objects.new(scene.name + '-anchors', mesh)
        # obj.draw_type = 'WIRE'
        # obj.show_x_ray = True
        scene.collection.objects.link(obj)

    def AddScenePortals(self, scene, data):
        portals_col = bpy.data.collections.new(scene.name + '-portals')
        scene.collection.children.link(portals_col)

        for portal in data.portals:
            bm = bmesh.new()

            tempVerts = []
            for vertex in portal.poly.v:
                pos = [vertex.pos.x, vertex.pos.y, vertex.pos.z]
                tempVerts.append(pos)

            # Switch the vertex order
            tempVerts[2], tempVerts[3] = tempVerts[3], tempVerts[2]

            bVerts = []
            for i in tempVerts:
                bVerts.append(bm.verts.new(arx_pos_to_blender_for_model(i)))

            bm.faces.new(bVerts)
            #bm.transform(correctionMatrix)
            mesh = bpy.data.meshes.new(scene.name + '-portal-mesh')
            bm.to_mesh(mesh)
            bm.free()
            obj = bpy.data.objects.new(scene.name + '-portal', mesh)
            obj.display_type = 'WIRE'
            obj.display.show_shadows = False
            # obj.show_x_ray = True
            # obj.hide = True
            #obj.parent_type = 'OBJECT'
            #obj.parent = groupObject
            portals_col.objects.link(obj)

    def AddSceneLights(self, scene, llfData, sceneOffset):
        lights_col = bpy.data.collections.new(scene.name + '-lights')
        scene.collection.children.link(lights_col)

        for index, light in enumerate(llfData.lights):
            light_name = scene.name + '-light_' + str(index).zfill(4)

            lampData = bpy.data.lights.new(name=light_name, type='POINT')
            lampData.color = (light.rgb.r, light.rgb.g, light.rgb.b)
            lampData.use_custom_distance = True
            lampData.cutoff_distance = light.fallend
            lampData.energy = light.intensity * 1000 # TODO this is a guessed factor

            obj = bpy.data.objects.new(name=light_name, object_data=lampData)
            lights_col.objects.link(obj)
            abs_loc = Vector(sceneOffset) + Vector([light.pos.x, light.pos.y, light.pos.z])
            obj.location = arx_pos_to_blender_for_model(abs_loc)


    def AddSceneObjects(self, scene, dlfData: DlfData, sceneOffset):
        entities_col = bpy.data.collections.new(scene.name + '-entities')
        scene.collection.children.link(entities_col)

        for e in dlfData.entities:
            
            legacyPath = e.name.decode('iso-8859-1').replace("\\", "/").lower().split('/')
            objectId = '/'.join(legacyPath[legacyPath.index('interactive') + 1 : -1])

            entityId = objectId + ":" + str(e.ident).zfill(4)
            self.log.info("Creating entity [{}]".format(entityId))

            proxyObject = bpy.data.objects.new(name='e:' + entityId, object_data=None)
            entities_col.objects.link(proxyObject)

            object_col = bpy.data.collections.get(objectId)
            if object_col:
                proxyObject.instance_type = 'COLLECTION'
                proxyObject.instance_collection = object_col
            else:
                proxyObject.show_name = True
                proxyObject.empty_display_type = 'ARROWS'
                proxyObject.empty_display_size = 20 #cm
                #self.log.info("Object not found: [{}]".format(objectId))
            
            setObjectPosition(proxyObject, sceneOffset, e.pos)
            setObjectRotation(proxyObject, e.angle)

    
    def getSceneObjects(self, result: DlfData, scene):
        entities_col = bpy.data.collections[scene.name + '-entities']
        for obj in entities_col.objects:
            foo = obj.name.split(':')
            if len(foo) != 3 or not foo[0] == 'e':
                self.log.warn('Invalid entity name in collection: {}'.format(obj.name))
                continue

            entity = DANAE_LS_INTER()
            entity.name = foo[1].encode('iso-8859-1')
            entity.ident = int(foo[2])

            getObjectPosition(obj, entity.pos)
            getObjectRotation(obj, entity.angle)

            # TODO read the rest of the entity data

            result.entities.append(entity)

    def AddPaths(self, scene, dlfData: DlfData, sceneOffset):
        col = bpy.data.collections.new(scene.name + '-paths')
        scene.collection.children.link(col)

        for arxPath in dlfData.paths:
            path = arxPath[0]
            name = path.name.decode('iso-8859-1')

            arxPathPoints = arxPath[1]
            curve = bpy.data.curves.new(scene.name + '-path-' + name, type='CURVE')
            curve.dimensions = '3D'

            spline = curve.splines.new('POLY')

            spline.points.add(len(arxPathPoints))
            for i, arxPathPoint in enumerate(arxPathPoints):
                arxPos = Vector([arxPathPoint.rpos.x, arxPathPoint.rpos.y, arxPathPoint.rpos.z])
                blePos = arx_pos_to_blender_for_model(arxPos)
                spline.points[i].co = (blePos[0], blePos[1], blePos[2], 1)

            if path.height == 0:
                spline.type = 'NURBS'
            else:
                # It's a zone !
                pass

            obj = bpy.data.objects.new(scene.name + '-path-' + name, curve)
            setObjectPosition(obj, sceneOffset, path.pos)
            col.objects.link(obj)


    def AddPlayer(self, scene, dlfData: DlfData, sceneOffset):
        obj = bpy.data.objects.new(name=scene.name + '-player', object_data=None)
        obj.show_name = True
        obj.empty_display_type = 'ARROWS'
        obj.empty_display_size = 60 #cm
        setObjectPosition(obj, sceneOffset, dlfData.playerPos)
        setObjectRotation(obj, dlfData.playerRot)
        scene.collection.objects.link(obj)

    def GetPlayer(self, scene, dlfData: DlfData):
        obj = bpy.data.objects[scene.name + '-player']
        getObjectPosition(obj, dlfData.playerPos)
        getObjectRotation(obj, dlfData.playerRot)

    def add_scene_map_camera(self, scene):
        """Grid size is 160x160m"""
        cam = bpy.data.cameras.new('Map Camera')
        cam.type = 'ORTHO'
        cam.ortho_scale = 16000
        cam.clip_start = 100 # 1m
        cam.clip_end = 20000 # 200m
        cam.show_name = True
        cam_obj = bpy.data.objects.new('Map Camera', cam)
        cam_obj.location = Vector((8000.0, 8000.0, 5000.0))
        scene.collection.objects.link(cam_obj)

        scene.render.engine = 'BLENDER_EEVEE'
        scene.render.resolution_x = 1000
        scene.render.resolution_y = 1000
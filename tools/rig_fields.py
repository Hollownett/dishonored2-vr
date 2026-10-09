"""Read-only rig decoding for the pinned Steam executable; never calls engine code."""
import math
import struct

GAME_HASH = "b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180"
RIG_TYPES = ("arkPlayer", "arkSkinnedRenderModel", "arkCpntAnimEvolved", "arkCpntPlayerBody", "arkCpntEquipment")


def pointer(read, address, alignment=8):
    value = struct.unpack("<Q", read(address, 8))[0]
    if not 0x10000 <= value < 0x0000800000000000 or value % alignment:
        raise ValueError("Invalid rig pointer")
    return value


def typed(read, address, base, tables):
    return bool(tables) and all(struct.unpack("<Q", read(address+t["subobject_offset"], 8))[0] ==
                               base+t["vtable_rva"] for t in tables)


def component_ready(raw_flags, required, forbidden):
    # Native 0x7e7390 permutes three state pairs before checking its mask table.
    flags = (((raw_flags >> 2) & 3) | ((raw_flags & 3) << 2)) << 2 | ((raw_flags >> 4) & 3)
    return flags & required == required and flags & forbidden == 0


def checked_component(read, address, required, forbidden):
    generation, _, target = struct.unpack("<iIQ", read(address, 16))
    if generation == -1:
        return None
    if not 0x10000 <= target < 0x0000800000000000 or target % 8:
        return None
    actual_generation, flags = struct.unpack("<iI", read(target+0x18, 8))
    return target if generation == actual_generation and component_ready(flags, required, forbidden) else None


def native_list(read, address, stride, limit):
    data, packed_count, packed_capacity = struct.unpack("<QII", read(address, 16))
    count, capacity = packed_count & 0xffffff, packed_capacity >> 8
    if count > capacity or capacity > limit or count > limit:
        raise ValueError("Invalid or excessive rig list")
    if not count:
        return 0, 0
    if not 0x10000 <= data < 0x0000800000000000 or data % min(stride, 8):
        raise ValueError("Invalid rig list pointer")
    return data, count


def decode_transform(data):
    values = struct.unpack("<16f", data)
    axes = [values[i:i+3] for i in (0, 4, 8)]
    position = values[12:15]
    # Havok's SIMD padding lanes need not contain meaningful floats.
    if not all(math.isfinite(v) and abs(v) < 1e7 for axis in axes+[position] for v in axis):
        raise ValueError("Invalid native joint transform")
    for i in range(3):
        for j in range(i, 3):
            if abs(sum(a*b for a, b in zip(axes[i], axes[j]))-(1 if i == j else 0)) > .03:
                raise ValueError("Native joint rotation is not orthonormal")
    a, b, c = axes
    determinant = a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])
    if abs(determinant-1) > .03:
        raise ValueError("Native joint rotation is reflected")
    return dict(position=list(position), rotation_columns=[list(axis) for axis in axes])


def joint_name(read, base, name_id):
    if name_id == 0xffff:
        return None
    manager = pointer(read, base+0x22995c0)
    if manager != base+0x22995d0 or struct.unpack("<Q", read(manager, 8))[0] != base+0x1b093c8:
        raise ValueError("Native joint-name manager differs")
    entries, count = native_list(read, manager+8, 0x48, 65535)
    if not 0 <= name_id < count:
        raise ValueError("Skeleton name ID lies outside the native pool")
    address = pointer(read, entries+name_id*0x48+0x10, 1)
    data = bytearray()
    while len(data) < 128:
        at = address+len(data)
        chunk = read(at, min(32, 128-len(data), 0x1000-(at & 0xfff)))
        if b'\0' in chunk:
            data.extend(chunk.split(b'\0', 1)[0])
            name = data.decode('utf-8')
            if not name or any(ord(c) < 32 for c in name):
                raise ValueError("Invalid skeleton joint name")
            return name
        data.extend(chunk)
    raise ValueError("Skeleton joint name exceeds the read limit")


def player_model(read, base, types):
    game = pointer(read, base+0x39a5290)
    if struct.unpack("<Q", read(game, 8))[0] != base+0x1c940a8:
        raise ValueError("Native game object changed")
    player = pointer(read, game+0x3130b0)
    if not typed(read, player, base, types["arkPlayer"]):
        raise ValueError("Native player type differs")
    model = pointer(read, player+0xf0)
    if pointer(read, player+0x360) != model or not typed(read, model, base, types["arkSkinnedRenderModel"]):
        raise ValueError("Player model references or type differ")
    return player, model


def animation_snapshot(read, address, base, types, ready_masks, pose_state=0):
    owner = player_model(read, base, types)
    return _animation_pose(read, address, base, types, ready_masks, owner, owner[1], pose_state)


def _animation_pose(read, address, base, types, ready_masks, owner, model, pose_state):
    if type(pose_state) is not int or not 0 <= pose_state < 4:
        raise ValueError('Unknown native pose state')
    if not typed(read, address, base, types["arkCpntAnimEvolved"]):
        raise ValueError("Animation component type changed")
    generation, flags = struct.unpack("<iI", read(address+0x18, 8))
    if generation == -1 or not component_ready(flags, *ready_masks):
        raise ValueError("Animation component is not in the native started state")
    player = owner[0]
    if pointer(read, address+0x38) != model or not typed(read, model, base, types['arkSkinnedRenderModel']):
        raise ValueError("Animation does not belong to the verified model")
    skeleton = pointer(read, pointer(read, model+0x1fe8)+0x50)
    joint_count = struct.unpack("<H", read(skeleton+0x78, 2))[0]
    if not 1 <= joint_count <= 2048:
        raise ValueError("Invalid skeleton joint count")
    names = struct.unpack("<"+"H"*joint_count, read(pointer(read, skeleton+0x70), joint_count*2))
    joint_names = [joint_name(read, base, name_id) for name_id in names]
    transforms, transform_count = native_list(read, address+0x2d8, 64, 4096)
    states, state_count = native_list(read, address+0x2e8, 16, 4)
    if pose_state >= state_count:
        raise ValueError("Animation does not contain the selected pose state")
    # +0x2e8 is the list of four pose-state lists, not a joint map. The native
    # IK bridge reads the first state's idList<short> through this extra level.
    mapping, map_count = native_list(read, states+pose_state*16, 2, 4096)
    if map_count < joint_count or not transform_count:
        raise ValueError("Animation has no complete joint mapping")
    indices = struct.unpack("<"+"h"*joint_count, read(mapping, joint_count*2))
    joints = []
    for joint, index in enumerate(indices):
        if index == -1:
            continue
        if not 0 <= index < transform_count:
            raise ValueError("Skeleton joint maps outside the transform array")
        transform_address = transforms+index*64
        joints.append(dict(joint=joint, name_id=names[joint], name=joint_names[joint], transform_index=index,
                           transform_address=transform_address, **decode_transform(read(transform_address, 64))))
    model_frame = struct.unpack('<15f', read(model+0x1860, 60))
    if not all(math.isfinite(v) and abs(v) < 1e7 for v in model_frame):
        raise ValueError('Invalid native model origin, axis or scale')
    # Detect replacement/recycling while the snapshot was assembled. These
    # checks establish read consistency, never permission to write animation.
    if read(address+0x18, 8) != struct.pack("<iI", generation, flags) or pointer(read, address+0x38) != model or \
            player_model(read, base, types) != owner:
        raise ValueError("Rig ownership changed while reading")
    return dict(player=player, model=model, animation=address, generation=generation, component_flags=flags,
                skeleton=skeleton, skeleton_joints=joint_count, joint_names=joint_names, sampled_joints=joints,
                model_origin=list(model_frame[:3]), model_axis_rows=[list(model_frame[i:i+3]) for i in (3,6,9)],
                model_scale=list(model_frame[12:]), pose_state=pose_state,
                pose_role='renderer_upload_source' if pose_state == 3 else 'intermediate_native_pose',
                final_blended_pose_verified=False, game_memory_writes=False,
                rig_binding_verified=False, snapshot_frame_synchronised=False,
                joint_space="Native hkTransform space; model/world relationship not yet calibrated")


def player_components(read, base, types):
    """Native 0x877720 traverses this bounded entity component-pointer list."""
    owner = player_model(read, base, types)
    player = owner[0]
    header = read(player+0x268, 16)
    data, count = native_list(read, player+0x268, 8, 4096)
    pointers = read(data, count*8) if count else b''
    known = {base+t['vtable_rva']: name for name, tables in types.items()
             for t in tables if t['subobject_offset'] == 0 and name.startswith('arkCpnt')}
    result = []
    for (address,) in struct.iter_unpack('<Q', pointers):
        if not address:
            continue
        table = struct.unpack('<Q', read(address, 8))[0]
        name = known.get(table)
        if name and typed(read, address, base, types[name]) and pointer(read, address+0x10) == player:
            result.append(dict(type=name, address=address, player_owner_verified=True))
    if read(player+0x268, 16) != header or (count and read(data, count*8) != pointers) or \
            player_model(read, base, types) != owner:
        raise ValueError('Player component list changed while reading')
    return result


def skeleton_parents(read, skeleton, base, joint_count):
    """Decode exact native 0x24f240; never invoke its virtual method."""
    if not 1 <= joint_count <= 2048 or pointer(read,skeleton)!=base+0x1b0bd08 or \
            pointer(read,base+0x1b0bd08+0x88)!=base+0x24f240:
        raise ValueError('Unsupported native skeleton hierarchy identity')
    blob=pointer(read,skeleton+0x158)
    relative_bytes=read(blob+0x1c,4)
    relative=struct.unpack('<I',relative_bytes)[0]
    if not 0 < relative <= 1024*1024:
        raise ValueError('Invalid skeleton hierarchy relative offset')
    mapping_bytes=read(blob+0x1c+relative,joint_count*2)
    mapping=struct.unpack('<'+'H'*joint_count,mapping_bytes)
    if any(index>=2048 for index in mapping):
        raise ValueError('Skeleton hierarchy record index exceeds inspection bound')
    # The compiled declaration may contain records outside this model's joint
    # subset. The native lookup remaps into that declaration, not a dense
    # 0..joint_count-1 list. Bound the observed record range independently.
    record_bytes=(max(mapping)+1)*4
    records=read(blob+0x40,record_bytes)
    parents=[]
    for index in mapping:
        parent=struct.unpack_from('<H',records,index*4+2)[0]&0x7fff
        parents.append(-1 if parent>=0x4000 else parent)
    if parents.count(-1)!=1 or any(parent>=joint_count for parent in parents):
        raise ValueError('Invalid skeleton hierarchy parent bounds or roots')
    for joint in range(joint_count):
        visited=set()
        while joint!=-1:
            if joint in visited:
                raise ValueError('Cyclic skeleton hierarchy')
            visited.add(joint);joint=parents[joint]
    if pointer(read,skeleton+0x158)!=blob or read(blob+0x1c,4)!=relative_bytes or \
            read(blob+0x1c+relative,joint_count*2)!=mapping_bytes or read(blob+0x40,record_bytes)!=records:
        raise ValueError('Skeleton hierarchy changed during reading')
    return parents


def affine_product(animated, inverse_bind):
    """Native 0x12e1d00: animated rows times inverse bind, bottom row 0,0,0,1."""
    if len(animated)!=12 or len(inverse_bind)!=12 or not all(
            math.isfinite(v) and abs(v)<1e7 for v in (*animated,*inverse_bind)):
        raise ValueError('Invalid native affine skinning matrix')
    return [sum(animated[row*4+k]*inverse_bind[k*4+column] for k in range(3))+
            (animated[row*4+3] if column==3 else 0.)
            for row in range(3) for column in range(4)]


def skinning_snapshot(read, pose, base):
    """Bounded passive inspection; no engine calls or frame-synchronisation claim."""
    skeleton,model,count=pose['skeleton'],pose['model'],pose['skeleton_joints']
    if count not in (20,69) or pose['pose_state']!=3 or pointer(read,skeleton)!=base+0x1b0bd08 or \
            len(pose['sampled_joints'])!=count:
        raise ValueError('Skinning inspection requires the complete pinned body/arm render-source rig')
    if count==20 and not {'center_spine03_jnt','center_head_jnt','left_ankle_jnt','right_ankle_jnt'}.issubset(pose.get('joint_names',[])):
        raise ValueError('The 20-joint player body semantics differ')
    inverse_address=pointer(read,skeleton+0x120,16)
    output_address=pointer(read,model+0x1ff8,16)
    # Native SIMD rounds arms to 72; only the declared joints carry meaning.
    inverse_bytes=read(inverse_address,count*48)
    output_bytes=read(output_address,count*48)
    inverse=[list(row) for row in struct.iter_unpack('<12f',inverse_bytes)]
    actual=[list(row) for row in struct.iter_unpack('<12f',output_bytes)]
    expected=[]
    source_unchanged=True
    for joint in pose['sampled_joints']:
        sampled=dict(position=joint['position'],rotation_columns=joint['rotation_columns'])
        current=decode_transform(read(joint['transform_address'],64))
        source_unchanged=source_unchanged and current==sampled
        columns=joint['rotation_columns']+[joint['position']]
        rows=[columns[column][row] for row in range(3) for column in range(4)]
        expected.append(affine_product(rows,inverse[joint['joint']]))
    if not all(math.isfinite(v) and abs(v)<1e7 for row in actual for v in row):
        raise ValueError('Invalid native output skinning matrix')
    if read(inverse_address,count*48)!=inverse_bytes or pointer(read,skeleton+0x120,16)!=inverse_address or \
            pointer(read,model+0x1ff8,16)!=output_address:
        raise ValueError('Native skinning buffer identity or inverse bind changed during reading')
    output_unchanged=read(output_address,count*48)==output_bytes
    error=max(abs(a-b) for arow,brow in zip(actual,expected) for a,b in zip(arow,brow))
    return dict(inverse_bind_address=inverse_address,output_address=output_address,
                inverse_bind_rows=inverse,output_rows=actual,joints=count,
                source_unchanged=source_unchanged,output_unchanged=output_unchanged,
                maximum_absolute_error=error if source_unchanged and output_unchanged else None,
                multiplication_order='animated_affine_times_inverse_bind',
                native_consumer_rva='0x24c340',native_multiply_rva='0x12e1d00',
                snapshot_frame_synchronised=False,mesh_skin_weights_verified=False,game_memory_writes=False)


def md6_surfaces(read, primary, base):
    """Bounded read-only metadata from the pinned 0x25aa00/0x259960 loaders."""
    if struct.unpack('<Q',read(primary,8))[0]!=base+0x1b0d118:
        raise ValueError('Native idMD6Model type differs')
    header=read(primary+0x58,16)
    data,count=native_list(read,primary+0x58,8,64)
    if not count: raise ValueError('Native arm mesh has no surfaces')
    entries=read(data,count*8)
    surfaces=[]
    for i in range(count):
        surface=pointer(read,data+i*8)
        prefix=read(surface,0xb8)
        geometry=struct.unpack_from('<Q',prefix,0x90)[0]
        if not geometry:
            # Native 0x259910 may discard the loaded CPU geometry after upload.
            surfaces.append(dict(surface=surface,geometry=None,kind=struct.unpack_from('<I',prefix,0x30)[0],
                vertices=struct.unpack_from('<I',prefix,0x54)[0],triangles=struct.unpack_from('<I',prefix,0x58)[0],
                cpu_geometry_available=False,skin_weights_verified=False,engine_methods_invoked=False,game_memory_writes=False))
            if read(surface,0xb8)!=prefix: raise ValueError('Native surface owner changed while inspecting')
            continue
        pointer(read,surface+0x90)
        geometry_header=read(geometry,0xa0)
        if struct.unpack_from('<Q',geometry_header)[0]!=base+0x1ab3a20:
            raise ValueError('Native MD6 geometry buffer type differs')
        index_count,vertex_count=struct.unpack_from('<2I',geometry_header,0x34)
        if not 0<vertex_count<=500000 or not 0<index_count<=3000000 or index_count%3:
            raise ValueError('Excessive or invalid native mesh counts')
        bounds=[struct.unpack_from('<3f',geometry_header,offset) for offset in (0x10,0x20)]
        if any(not math.isfinite(v) or abs(v)>1e6 for point in bounds for v in point) or \
                any(a>b for a,b in zip(*bounds)):
            raise ValueError('Native mesh bounds differ')
        format_offset=0x70 if struct.unpack_from('<Q',geometry_header,0x70)[0] else 0x90
        try:
            vertex_format=pointer(read,geometry+format_offset)
        except ValueError as error:
            raise ValueError(f'Native surface {i} has no validated vertex-format pointer') from error
        format_prefix=read(vertex_format,0x18)
        stride,format_id=struct.unpack_from('<2H',format_prefix,0x10)
        if not 1<=stride<=256: raise ValueError('Native vertex stride differs')
        if format_id>12 or pointer(read,base+0x32d55a0+0xd0+format_id*8)!=vertex_format:
            raise ValueError('Native vertex format no longer belongs to its registry')
        element_data,element_count=native_list(read,vertex_format,8,32)
        if not element_count: raise ValueError('Native mesh format has no vertex elements')
        element_raw=read(element_data,element_count*8)
        elements=[]
        for element in range(element_count):
            packed,semantic,index,offset,slot=struct.unpack_from('<I4B',element_raw,element*8)
            if semantic>=32: raise ValueError('Native vertex semantic exceeds bounded table')
            semantic_ptr=pointer(read,base+0x229ab40+semantic*8,1)
            name=read(semantic_ptr,32).split(b'\0',1)[0].decode('ascii')
            if not name or len(name)>=32 or any(not(c.isupper() or c.isdigit() or c=='_') for c in name):
                raise ValueError('Native vertex semantic name differs')
            elements.append(dict(semantic=name,index=index,offset=offset,input_slot=slot,packed_format=packed))
        cpu_vertices=struct.unpack_from('<Q',geometry_header,0x80)[0]
        vertex_prefix=None
        if cpu_vertices:
            if pointer(read,geometry+0x80,1)!=cpu_vertices: raise ValueError('Native vertex owner differs')
            vertex_prefix=read(cpu_vertices,min(vertex_count,16)*stride).hex()
        indices=struct.unpack_from('<Q',geometry_header,0x88)[0]
        index_prefix=None
        if indices:
            pointer(read,geometry+0x88,2)
            index_prefix=read(indices,min(index_count,64)*2).hex()
        ranges,range_count=native_list(read,surface+0xa8,12,1024)
        range_data=read(ranges,range_count*12) if range_count else b''
        if read(surface,0xb8)!=prefix or read(geometry,0xa0)!=geometry_header or read(vertex_format,0x18)!=format_prefix or \
                read(element_data,element_count*8)!=element_raw:
            raise ValueError('Native surface identity changed while inspecting')
        surfaces.append(dict(surface=surface,geometry=geometry,kind=struct.unpack_from('<I',prefix,0x30)[0],
            vertices=vertex_count,indices=index_count,bounds=bounds,vertex_format=vertex_format,
            vertex_stride=stride,format_id=format_id,vertex_elements=elements,format_role='CPU' if format_offset==0x70 else 'GPU',format_prefix_hex=format_prefix.hex(),cpu_vertex_prefix_hex=vertex_prefix,
            index_prefix_hex=index_prefix,draw_range_records=[list(struct.unpack_from('<3I',range_data,j*12)) for j in range(range_count)],
            skin_weights_verified=False,engine_methods_invoked=False,game_memory_writes=False))
    if read(primary+0x58,16)!=header or read(data,count*8)!=entries or \
            struct.unpack('<Q',read(primary,8))[0]!=base+0x1b0d118:
        raise ValueError('Native MD6 surface owner changed while inspecting')
    return surfaces


def model_geometry_layout(read, pose, base):
    """Shallow owned-resource inspection; opaque prefixes are not decoded vertices."""
    model,skeleton=pose['model'],pose['skeleton']
    descriptor=pointer(read,model+0x1fe8)
    prefix=read(descriptor,0x100)
    if struct.unpack_from('<Q',prefix,0x50)[0]!=skeleton:
        raise ValueError('Model resource no longer points to the owned arm skeleton')
    # Constructor 0x25d090 passes descriptor+8 to 0x2368a0, which copies
    # this pointer into model+0x1bb0. It requires the +0x68 list's first
    # geometry pointer to be present. No unknown list stride is assumed.
    source=pointer(read,descriptor+8)
    if pointer(read,model+0x1bb0)!=source:
        raise ValueError('Native model geometry source differs from its descriptor')
    geometry_list=pointer(read,descriptor+0x68)
    list_header=read(geometry_list,16)
    data,count=native_list(read,geometry_list,8,16)
    if not count: raise ValueError('Arm geometry source list is empty')
    primary=pointer(read,data)
    source_prefix=read(source,0x100)
    primary_prefix=read(primary,0x100)
    if read(descriptor,0x100)!=prefix or pointer(read,model+0x1fe8)!=descriptor or \
            pointer(read,model+0x1bb0)!=source or read(geometry_list,16)!=list_header or \
            pointer(read,data)!=primary:
        raise ValueError('Arm geometry resource identity changed while inspecting')
    result=dict(descriptor=descriptor,descriptor_prefix_hex=prefix.hex(),source=source,
                source_prefix_hex=source_prefix.hex(),geometry_list_count=count,
                primary_geometry=primary,primary_geometry_prefix_hex=primary_prefix.hex(),
                native_constructor_rva='0x25d090',source_copy_rva='0x2368a0',
                vertex_layout_verified=False,skin_weights_verified=False,engine_methods_invoked=False,
                game_memory_writes=False)
    try:
        result['surfaces']=md6_surfaces(read,primary,base)
    except (OSError,ValueError,struct.error) as error:
        result['surface_layout_error']=str(error)
    if pointer(read,model+0x1fe8)!=descriptor or read(descriptor,0x100)!=prefix or read(geometry_list,16)!=list_header:
        raise ValueError('Arm resource owner changed during surface inspection')
    return result


def pose_layout_snapshot(read, pose, base):
    """Inspect an already-owned native body/arm pose; no engine calls or writes."""
    skeleton=pose['skeleton']
    prefix=read(skeleton, 0x130)
    table=struct.unpack_from('<Q',prefix)[0]
    if not base <= table < base+0x41cb000-0x90:
        raise ValueError('Native skeleton virtual table lies outside the pinned game image')
    method=pointer(read,table+0x88)
    if not base <= method < base+0x41cb000:
        raise ValueError('Skeleton method lies outside the pinned game image')
    headers=[]
    for header_offset in range(0x70,0x121,0x10):
        data,count,capacity=struct.unpack_from('<QII',prefix,header_offset)
        headers.append(dict(offset=header_offset,data=data,raw_count=count,raw_capacity=capacity))
    parents=skeleton_parents(read,skeleton,base,pose['skeleton_joints'])
    pose['skeleton_layout']=dict(vtable_rva=table-base,method_slot_88_rva=method-base,
        prefix_hex=prefix.hex(),candidate_headers=headers,
        parent_indices=parents,parent_hierarchy_verified=True,engine_methods_invoked=False)
    if pose['pose_state']==3 and pose['skeleton_joints'] in (20,69):
        pose['skinning_layout']=skinning_snapshot(read,pose,base)
        try:
            pose['geometry_layout']=model_geometry_layout(read,pose,base)
        except (OSError,ValueError,struct.error) as error:
            pose['geometry_layout']=dict(error=str(error),game_memory_writes=False)
    if read(skeleton,0x130)!=prefix or pointer(read,pointer(read,pose['model']+0x1fe8)+0x50)!=skeleton:
        raise ValueError('Native skeleton changed during layout inspection')
    return pose


def body_snapshot(read, body, base, types, ready_masks, pose_state=0, inspect_skeleton=False):
    """Resolve separate arm rigs through the player's own body component."""
    owner = player_model(read, base, types)
    body_frame_bytes=read(owner[1]+0x1860,60)
    body_frame=struct.unpack('<15f',body_frame_bytes)
    if not all(math.isfinite(v) and abs(v)<1e7 for v in body_frame):
        raise ValueError('Invalid owned player model frame')
    if not typed(read, body, base, types['arkCpntPlayerBody']) or pointer(read, body+0x10) != owner[0]:
        raise ValueError('Body component type or player owner differs')
    animation = pointer(read, body+0x58)
    if not typed(read, animation, base, types['arkCpntAnimEvolved']) or pointer(read, animation+0x38) != owner[1]:
        raise ValueError('Body animation does not belong to the verified player model')
    body_identity = read(body+0x18, 8)
    generation, flags = struct.unpack('<iI', body_identity)
    if generation == -1 or not component_ready(flags, *ready_masks):
        raise ValueError('Player body is not started')
    arms = []
    for side, offset in (('left', 0x370), ('right', 0x680)):
        arm_animation = struct.unpack('<Q', read(body+offset+8, 8))[0]
        if not arm_animation:
            arms.append(dict(side=side, animation=None, rig_allocated=False))
            continue
        if arm_animation % 8 or not typed(read, arm_animation, base, types['arkCpntAnimEvolved']):
            raise ValueError('Arm animation component type differs')
        arm_identity = read(arm_animation+0x18, 8)
        arm_generation, arm_flags = struct.unpack('<iI', arm_identity)
        if arm_generation == -1 or not component_ready(arm_flags, *ready_masks):
            raise ValueError('Arm animation is not started')
        model = pointer(read, arm_animation+0x38)
        if not typed(read, model, base, types['arkSkinnedRenderModel']):
            raise ValueError('Arm skinned model type differs')
        pose = _animation_pose(read, arm_animation, base, types, ready_masks, owner, model, pose_state)
        if inspect_skeleton: pose_layout_snapshot(read,pose,base)
        if read(arm_animation+0x18, 8) != arm_identity or pointer(read, arm_animation+0x38) != model or \
                struct.unpack('<Q', read(body+offset+8, 8))[0] != arm_animation:
            raise ValueError('Arm ownership changed while reading')
        arms.append(dict(side=side, rig_allocated=True, **pose))
    if read(body+0x18, 8) != body_identity or pointer(read, body+0x10) != owner[0] or \
            pointer(read, body+0x58) != animation or \
            player_model(read, base, types) != owner:
        raise ValueError('Body ownership changed while reading')
    return dict(player=owner[0], model=owner[1], body=body, arms=arms,
                player_model_frame=dict(origin=list(body_frame[:3]),axis_rows=[list(body_frame[i:i+3]) for i in (3,6,9)],
                    scale=list(body_frame[12:]),read_consistent=read(owner[1]+0x1860,60)==body_frame_bytes,
                    camera_anchor_role_verified=False),
                game_memory_writes=False, rig_binding_verified=False, snapshot_frame_synchronised=False)

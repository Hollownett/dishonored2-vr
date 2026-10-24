import json
import itertools
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from simulated_gameplay_script import compile_script,validate_script_profile
import trace_to_script
from inject_probe import probe_config,preserve_probe_outputs
from native_left_aim_profile_check import profile


def script_profile(**changes):
    values=dict(native_left_aim_experiment=False,simulated_vr=True,simulated_input=True,
                simulated_gameplay_script=Path('test.json'))
    values.update(changes)
    return profile(**values)


class GameplayScriptProfileCheck(unittest.TestCase):
    def compile(self,data,seconds=10):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'script.json'
            path.write_text(data if isinstance(data,str) else json.dumps(data),encoding='utf-8')
            return compile_script(path,root,seconds)

    def test_valid_transport_and_all_fields(self):
        data={'segments':[{'start_ms':1000,'end_ms':2000,'input':{'move_y':.5,'left_trigger':.2,'a':True}},
                          {'start_ms':3000,'end_ms':4000,'input':{'turn_x':-.5,'left_click':True}}]}
        source,transport=self.compile(data)
        self.assertEqual(json.loads(source),data)
        self.assertEqual(transport,'D2VR_GAMEPLAY_SCRIPT 1 10000 2\n1000 2000 0 0.5 0 0 0.2 0 0 0 1 0 0 0 0 0 0\n3000 4000 0 0 -0.5 0 0 0 0 0 0 0 0 0 1 0 0\n')

    def test_validation_rejects_nonfinite_unknown_and_overlap(self):
        valid={'start_ms':1000,'end_ms':2000,'input':{'move_y':.5}}
        for segments in ([],[dict(valid,start_ms=True)],[dict(valid,end_ms=10001)],
                         [valid,dict(valid,start_ms=1500,end_ms=2500)],
                         [dict(valid,input={'move_y':float('nan')})],[dict(valid,input={'move_y':float('inf')})],
                         [dict(valid,input={'move_y':10**1000})],[dict(valid,input={'move_y':True})],
                         [dict(valid,input={'left_trigger':-.1})],[dict(valid,input={'a':1})],
                         [dict(valid,input={'unknown':1})],[dict(valid,input={})],[valid]*129):
            with self.subTest(segments=str(segments)[:100]),self.assertRaises(ValueError):
                self.compile({'segments':segments})
        with self.assertRaises(ValueError):self.compile('{"segments":[],"segments":[]}')
        with self.assertRaises(ValueError):self.compile({'segments':[valid],'unrecognized':0})

    def test_workspace_and_size_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'large.json';path.write_text(' '*65537)
            with self.assertRaises(ValueError):compile_script(path,root,10)
            with self.assertRaises(ValueError):compile_script(path,root/'nested',10)

    def test_optional_head_pitch_v2_and_v1_compatibility(self):
        segment={'start_ms':1000,'end_ms':2000,'input':{'x':True},'head_pitch_degrees':-60}
        _,transport=self.compile({'segments':[segment,{'start_ms':2000,'end_ms':3000,'input':{'x':False}}]})
        rows=transport.splitlines()
        self.assertEqual(rows[0],'D2VR_GAMEPLAY_SCRIPT 2 10000 2')
        self.assertEqual(rows[1].split()[-1],'-60');self.assertEqual(rows[2].split()[-1],'0')
        for pitch in (-80,80,0,12.5):
            self.compile({'segments':[dict(segment,head_pitch_degrees=pitch)]})
        for pitch in (-80.01,80.01,float('nan'),float('inf'),10**1000,True,None,'-60'):
            with self.subTest(pitch=str(pitch)[:30]),self.assertRaises(ValueError):
                self.compile({'segments':[dict(segment,head_pitch_degrees=pitch)]})

    def test_when_v6(self):
        seg=lambda i,**k:dict({'start_ms':i*10,'end_ms':i*10+10,'input':{'a':True}},**k)
        _,legacy=self.compile({'segments':[seg(0,when='gameplay')]})
        self.assertEqual(legacy.splitlines()[0],'D2VR_GAMEPLAY_SCRIPT 1 10000 1')  # default keeps the old version
        for name,code in (('menu','1'),('always','2')):
            _,transport=self.compile({'segments':[seg(0),seg(1,when=name)]})
            rows=transport.splitlines();self.assertEqual(rows[0],'D2VR_GAMEPLAY_SCRIPT 6 10000 2')
            self.assertEqual((len(rows[1].split()),rows[1].split()[-1],rows[2].split()[-1]),(30,'0',code))
        for bad in ('menus',1,None,True,''):
            with self.subTest(when=str(bad)),self.assertRaises(ValueError):
                self.compile({'segments':[seg(0,when=bad)]})

    def test_touch_v7(self):
        seg=lambda i,**k:dict({'start_ms':i*10,'end_ms':i*10+10,'input':{'a':True}},**k)
        _,old=self.compile({'segments':[seg(0,when='menu')]})
        self.assertEqual(old.splitlines()[0],'D2VR_GAMEPLAY_SCRIPT 6 10000 1')  # no touch key: the older version, loads as before
        _,transport=self.compile({'segments':[seg(0),seg(1,touch={'right_trigger':True,'left_thumbrest':True})]})
        rows=transport.splitlines();self.assertEqual(rows[0],'D2VR_GAMEPLAY_SCRIPT 7 10000 2')
        self.assertEqual(rows[1].split()[-10:],['0']*10)
        self.assertEqual(rows[2].split()[-10:],['0','0','0','0','1','1','0','0','0','0'])
        self.assertEqual(len(rows[2].split()),40)
        for bad in ({'right_pinky':True},{'right_trigger':1},{'right_trigger':None},[],'x'):
            with self.subTest(touch=str(bad)),self.assertRaises(ValueError):
                self.compile({'segments':[seg(0,touch=bad)]})

    def test_absolute_poses_v5_and_trace_conversion(self):
        pose=[0,1.65,0,0,0,0,1]
        seg=lambda i,**k:dict({'start_ms':i*10,'end_ms':i*10+10,'input':{'x':True},'head_pose':pose,'right_hand_pose':[1,2,3,0,0,0,1]},**k)
        _,transport=self.compile({'segments':[seg(0)]})
        rows=transport.splitlines();self.assertEqual(rows[0],'D2VR_GAMEPLAY_SCRIPT 5 10000 1')
        fields=rows[1].split()  # 2 times, 8 analog, 7 buttons, head pitch, 9 offsets, hand pitch, mask, 2 poses
        self.assertEqual((len(fields),fields[28]),(28+1+14,'5'))
        _,transport=self.compile({'segments':[seg(0),dict(seg(1),head_pose=None)]})
        self.assertEqual(transport.splitlines()[2].split()[28:],['4']+['1','2','3','0','0','0','1'])
        self.compile({'segments':[seg(i) for i in range(1000)]})
        with self.assertRaises(ValueError):  # more than 128 segments need the pose format
            self.compile({'segments':[{'start_ms':i*10,'end_ms':i*10+10,'input':{'x':True}} for i in range(129)]})
        for bad in ([0,0,0,0,0,0,2],[0,0,0,0,0,0],[0,0,0,0,0,0,0],[float('nan'),0,0,0,0,0,1],[11,0,0,0,0,0,1],[0,0,0,0,0,0,'1'],[0,0,0,0,0,0,True],'pose'):
            with self.subTest(pose=str(bad)),self.assertRaises(ValueError):
                self.compile({'segments':[seg(0,head_pose=bad)]})
        # Real-trace conversion: recentring maths, then a converted slice is a valid script.
        trace_to_script.selfcheck()
        trace=[dict(tick=1000+33*i,logic=2,tracked_hands=3,buttons=1 if i==1 else 0,left_grip=0,right_grip=1.0 if i==1 else 0,left_trigger=0,right_trigger=0,
                    move_x=0,move_y=-.5,turn_x=0,turn_y=0,**{p+c:v for p,pose in (('head_',[1,1.7,2,0,0,0,1]),('left_',[.8,1,1.6,0,0,0,1]),('right_',[1.2,1,1.6,0,0,0,1]))
                    for c,v in zip(('x','y','z','qx','qy','qz','qw'),pose)}) for i in range(4)]
        trace[3]['tick']+=5000  # a long pause is compressed
        segments,yaw=trace_to_script.build_segments(trace,2000)
        self.assertEqual(yaw,0);self.assertEqual([(s['start_ms'],s['end_ms']) for s in segments][:3],[(0,2000),(2000,2033),(2033,2066)])
        self.assertEqual(segments[-1]['end_ms'],2000+33+33+trace_to_script.GAP_MS+33)
        self.assertEqual(segments[1]['head_pose'],[0,1.65,0,0,0,0,1]);self.assertEqual(segments[1]['right_hand_pose'],[.2,.95,-.4,0,0,0,1])
        self.assertEqual(segments[2]['input'],{'move_y':-.5,'right_grip':1.0,'a':True})
        self.compile({'segments':segments})

    def test_profile_guards_and_default_off(self):
        validate_script_profile(script_profile())
        validate_script_profile(profile(simulated_gameplay_script=None,seconds=0))
        for change in (dict(simulated_vr=False),dict(simulated_input=False),dict(paired=False),
                       dict(native_eye_capture=False),dict(vr_preview=False),dict(head_tracking=False),
                       dict(controllers=False),dict(dll_name=None),dict(until_stopped=True),dict(seconds=9),
                       dict(seconds=121),dict(simulated_cleanup_exception=True),
                       dict(saved_settings=True)):
            with self.subTest(change=change),self.assertRaises(ValueError):validate_script_profile(script_profile(**change))

    def test_explicit_production_feature_combinations(self):
        for arms,grab,aim in itertools.product((False,True),repeat=3):
            with self.subTest(arms=arms,grab=grab,aim=aim):
                args=script_profile(tracked_arms=arms,physical_grab=grab,controller_aim=aim)
                validate_script_profile(args)
                fields=probe_config(args,4096,6,1,0,2).split()
                self.assertEqual(fields[30],'1')
                self.assertEqual(len(fields),32)

    def test_append_only_flag_index30(self):
        on=probe_config(script_profile(),4096,6,1,0,2).split()
        off=probe_config(script_profile(simulated_gameplay_script=None),4096,6,1,0,2).split()
        self.assertEqual(len(on),32);self.assertEqual(on[:30],off[:30])
        self.assertEqual((on[30],off[30]),('1','0'))

    def test_archives_commands_and_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'artifacts').mkdir()
            for name in ('simulated-gameplay-script.json','simulated-gameplay-commands.csv'):
                (root/'artifacts'/name).write_text('command only')
            archive=preserve_probe_outputs(root,'test')
            self.assertEqual(len(list(archive.iterdir())),2)


if __name__=='__main__':unittest.main()

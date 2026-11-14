import json, pathlib, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class TestStaticContract(unittest.TestCase):
    def test_json(self):
        j=json.loads((ROOT/'configs/PluginInfo.json').read_text())
        self.assertEqual(j['MinApiVersion'],2.11)
    def test_commands(self):
        source=(ROOT/'src/Main.cpp').read_text()
        for cmd in ['/tribeup','/tribeaccept','/tribedecline']:
            self.assertIn(cmd,source)
        self.assertIn('AddToTribe(&tribe, false, false, true, inviter)',source)
        self.assertIn('AllowAddToTribe',source)
        self.assertIn('kMaxMembers = 6',source)
        self.assertIn('SoleMemberOfOldTribe',source)
if __name__=='__main__': unittest.main()

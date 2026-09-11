import tests.core, os, subprocess, time

BIN_DIR = os.path.abspath(os.path.join(os.path.split(__file__)[0], '../../src/out/host'))
SOCK_A = '_urelay_test_A.sock'
SOCK_B = '_urelay_test_B.sock'
PORT_A='48001'
PORT_B='48002'

class UrelayTest(tests.core.BeepTest):
    name = "Urelay Test"

    def test(self):
        # Stop all, test does not depend on virtual devices
        tests.core.stop_all(self.dtab)
        self.CWD = os.getcwd()
        try:
            os.chdir(BIN_DIR)
            ubusd_A = subprocess.Popen(['ubusd','-s',SOCK_A])
            ubusd_B = subprocess.Popen(['ubusd','-s',SOCK_B])

            urelay_A = subprocess.Popen(['./urelay','--ubus=' + SOCK_A,
                    '--urelay_port=' + PORT_A])
            urelay_B = subprocess.Popen(['./urelay','--ubus=' + SOCK_B,
                    '--urelay_port=' + PORT_B])

            time.sleep(2)
            self.log('Test daemons started')

        finally:
            self.log('Cleanup')
            os.remove(SOCK_A)
            os.remove(SOCK_B)
            urelay_A.terminate()
            ubusd_A.terminate()
            urelay_B.terminate()
            ubusd_B.terminate()

            os.chdir(self.CWD)

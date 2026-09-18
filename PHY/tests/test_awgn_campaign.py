"""Campaign ingestion must not turn broken/incomplete runs into valid data."""
import unittest
from run_awgn_campaign import cases, parse_output


class CampaignParserTests(unittest.TestCase):
    def setUp(self):
        self.case = next(c for c in cases() if c['id'] == 'pdsch_dmrs_qpsk_mmse')

    def output(self, rows):
        return f'SNR (dB) BER BLER\n------------------\n{rows}\nSimulation complete.\n'

    def test_valid_scientific_notation(self):
        rows = parse_output(self.output('-5.0 1.2500e-02 0.2500\n0.0 0.0 0.0'), self.case, [-5, 0])
        self.assertEqual(rows[0], (-5.0, 'ber', '1.2500e-02'))
        self.assertEqual(len(rows), 4)

    def test_reject_missing_duplicate_and_reordered_points(self):
        for rows in ['-5 0 0', '-5 0 0\n-5 0 0', '0 0 0\n-5 0 0']:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                parse_output(self.output(rows), self.case, [-5, 0])

    def test_reject_bad_values_and_column_count(self):
        for row in ['0 nan 0', '0 0 inf', '0 0 -0.1', '0 0 1.1', '0 0 0 0']:
            with self.subTest(row=row), self.assertRaises(ValueError):
                parse_output(self.output(row), self.case, [0])

    def test_reject_incomplete_process_output(self):
        with self.assertRaises(ValueError):
            parse_output('SNR (dB) BER BLER\n0 0 0\n', self.case, [0])

    def test_reject_inconsistent_harq(self):
        case = next(c for c in cases() if c['id'] == 'pdsch_harq_ir')
        with self.assertRaises(ValueError):
            parse_output(self.output('0 0 0.1 0.2 2'), case, [0])

    def test_reject_inconsistent_pdcch(self):
        case = next(c for c in cases() if c['id'] == 'pdcch_css_al4')
        with self.assertRaises(ValueError):
            parse_output(self.output('0 0.8 0.5 0 0'), case, [0])

    def test_olla_reads_summaries_not_time_series(self):
        case = next(c for c in cases() if c['id'] == 'pdsch_olla')
        output = ('Trial MCS Offset WindowBLER CumBLER\n10 5 0.1 0.2 0.2\n'
                  'Open-Loop 최종: BER=1e-2 BLER=0.2 (목표 0.1) 평균 MCS=5.00\n'
                  'OLLA 최종: BER=1e-3 BLER=0.1 (목표 0.1) 평균 MCS=4.50\n'
                  'PDSCH OLLA simulation complete.\n')
        rows = parse_output(output, case, [5])
        self.assertEqual(len(rows), 6)
        self.assertEqual(rows[-1], (5, 'olla_avg_mcs', '4.50'))


if __name__ == '__main__':
    unittest.main()

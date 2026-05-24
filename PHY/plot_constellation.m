% plot_constellation.m  -  5G NR PHY constellation viewer + EVM calculator
%
% Usage:
%   1. Enable IQ dump in config/sim_config.txt:
%        IQ_DUMP = 1
%        IQ_DUMP_SNR = 12.0   (must match a simulated SNR point)
%   2. Run simulator: ./lls_sim > result.txt
%   3. In MATLAB (PHY directory):
%        plot_constellation          % reads 'iq_dump.txt'
%        plot_constellation('file.txt')

function plot_constellation(varargin)

script_dir = fileparts(mfilename('fullpath'));

if nargin == 0
    fname = fullfile(script_dir, 'iq_dump.txt');
else
    fname = varargin{1};
end

if ~isfile(fname)
    error('IQ dump file not found: %s\nRun simulator with IQ_DUMP=1 first.', fname);
end

% --- Parse IQ dump file ---
fid = fopen(fname, 'r');
mod_name = '';
snr_val  = nan;
tx_data  = [];
rx_data  = [];

while ~feof(fid)
    line = fgetl(fid);
    if ~ischar(line), break; end
    line = strtrim(line);

    if startsWith(line, '#')
        tok = regexp(line, 'MOD=(\S+)', 'tokens');
        if ~isempty(tok), mod_name = tok{1}{1}; end
        tok = regexp(line, 'SNR=([\d.eE+-]+)', 'tokens');
        if ~isempty(tok), snr_val = str2double(tok{1}{1}); end
        continue;
    end

    nums = sscanf(line, '%f %f %f %f');
    if numel(nums) == 4
        tx_data(end+1, :) = [nums(1) nums(2)]; %#ok<AGROW>
        rx_data(end+1, :) = [nums(3) nums(4)]; %#ok<AGROW>
    end
end
fclose(fid);

if isempty(tx_data)
    error('No IQ data found in: %s', fname);
end

% --- EVM Calculation ---
% EVM_rms (%) = sqrt(mean(|rx - tx|^2)) / sqrt(mean(|tx|^2)) * 100
% EVM_peak(%) = max(|rx - tx|)          / sqrt(mean(|tx|^2)) * 100
error_vec   = rx_data - tx_data;
error_mag   = sqrt(sum(error_vec.^2, 2));
ref_rms     = sqrt(mean(sum(tx_data.^2, 2)));

evm_rms_pct  = 100 * sqrt(mean(error_mag.^2)) / ref_rms;
evm_peak_pct = 100 * max(error_mag)            / ref_rms;
evm_rms_db   = 20*log10(evm_rms_pct/100);

% --- Console output ---
fprintf('=== EVM Results ===\n');
fprintf('Modulation  : %s\n',  mod_name);
fprintf('SNR         : %.1f dB\n', snr_val);
fprintf('N symbols   : %d\n',  size(tx_data,1));
fprintf('EVM rms     : %.3f%%  (%.2f dB)\n', evm_rms_pct,  evm_rms_db);
fprintf('EVM peak    : %.3f%%\n', evm_peak_pct);

% --- Figure: Constellation ---
tx_c  = tx_data(:,1) + 1j*tx_data(:,2);
rx_c  = rx_data(:,1) + 1j*rx_data(:,2);
ideal = unique(tx_c);

fig = figure('Name', sprintf('Constellation | %s @ %.1f dB SNR', mod_name, snr_val), ...
             'NumberTitle', 'off', 'Position', [100 100 1200 520]);

% ---- subplot 1: Constellation scatter ----
subplot(1,2,1);
scatter(real(rx_c), imag(rx_c), 4, [0.25 0.55 0.95], 'filled', ...
    'MarkerFaceAlpha', 0.25);
hold on;
scatter(real(ideal), imag(ideal), 80, 'r', 'x', 'LineWidth', 2);
hold off;
grid on; axis equal;
ax_lim = max(abs([real(ideal); imag(ideal)])) * 1.8;
xlim([-ax_lim  ax_lim]);
ylim([-ax_lim  ax_lim]);
xlabel('In-Phase (I)',     'FontSize', 11);
ylabel('Quadrature (Q)',   'FontSize', 11);
title(sprintf('%s Constellation  @ SNR = %.1f dB', mod_name, snr_val), 'FontSize', 12);
legend({'Received', 'Ideal'}, 'Location', 'northeast', 'FontSize', 9);

% ---- subplot 2: EVM histogram ----
subplot(1,2,2);
histogram(100 * error_mag / ref_rms, 60, 'FaceColor', [0.35 0.65 0.35], ...
    'EdgeColor', 'none', 'Normalization', 'probability');
hold on;
yl = ylim;
plot([evm_rms_pct  evm_rms_pct],  yl, 'r-',  'LineWidth', 1.8);
plot([evm_peak_pct evm_peak_pct], yl, 'm--', 'LineWidth', 1.4);
text(evm_rms_pct,  yl(2)*0.95, sprintf(' EVM_{rms}=%.2f%%',  evm_rms_pct),  ...
     'Color', 'r', 'FontSize', 9, 'HorizontalAlignment', 'left');
text(evm_peak_pct, yl(2)*0.85, sprintf(' EVM_{peak}=%.2f%%', evm_peak_pct), ...
     'Color', 'm', 'FontSize', 9, 'HorizontalAlignment', 'left');
hold off;
grid on;
xlabel('EVM per symbol (%)', 'FontSize', 11);
ylabel('Probability',        'FontSize', 11);
title(sprintf('EVM Distribution  (rms = %.2f dB)', evm_rms_db), 'FontSize', 12);

annotation('textbox', [0 0.95 1 0.05], ...
    'String', sprintf('5G NR PHY  |  %s  |  SNR = %.1f dB  |  N = %d symbols', ...
        mod_name, snr_val, size(tx_data,1)), ...
    'EdgeColor', 'none', 'HorizontalAlignment', 'center', ...
    'FontSize', 12, 'FontWeight', 'bold');

end % function plot_constellation

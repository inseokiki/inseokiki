% plot_ber.m  -  Uncoded BER vs SNR plotter (for ber_sim output)
%
% Usage:
%   ./ber_sim > ber_result.txt
%   plot_ber              % reads 'ber_result.txt' in same directory
%   plot_ber('myfile.txt')
%   plot_ber('qpsk.txt', '16qam.txt', '64qam.txt')  % overlay multiple

function plot_ber(varargin)

script_dir = fileparts(mfilename('fullpath'));

if nargin == 0
    files = { fullfile(script_dir, 'ber_result_QPSK.txt'), ...
              fullfile(script_dir, 'ber_result_16QAM.txt'), ...
              fullfile(script_dir, 'ber_result_64QAM.txt') };
else
    files = varargin;
end

colors  = lines(numel(files));
markers = {'o', 's', '^'};

figure('Name', 'BER vs SNR  |  5G NR PHY LLS', 'NumberTitle', 'off', ...
       'Position', [200 200 800 550]);
hold on;

legend_entries = {};

for fi = 1:numel(files)
    fname = files{fi};
    if ~isfile(fname)
        fprintf('[WARN] File not found: %s\n', fname);
        continue;
    end

    [snr, ber, ~, ~, mod_name] = parse_ber_output(fname);

    if isempty(snr)
        fprintf('[WARN] No data: %s\n', fname);
        continue;
    end

    mk = markers{mod(fi-1, numel(markers)) + 1};
    c  = colors(fi,:);

    % Drop BER=0 points
    valid = ber > 0;
    semilogy(snr(valid), ber(valid), ...
             'Color', c, 'LineWidth', 2, ...
             'Marker', mk, 'MarkerSize', 7, 'MarkerFaceColor', c);
    legend_entries{end+1} = [mod_name ' (sim)'];

    % Theory BER (dashed, same color)
    snr_th = linspace(min(snr)-2, max(snr)+2, 500);
    ber_th = theory_ber(mod_name, snr_th);
    if ~isempty(ber_th)
        semilogy(snr_th, max(ber_th, 1e-7), '--', ...
                 'Color', c, 'LineWidth', 1.2);
        legend_entries{end+1} = [mod_name ' (theory)'];
    end
end

hold off;
grid on;
set(gca, 'YScale', 'log', 'YMinorGrid', 'on', 'FontSize', 11);
xlabel('Eb/N0 (dB)', 'FontSize', 13);
ylabel('BER',        'FontSize', 13);
title('BER vs SNR  —  5G NR PHY Link Level Simulation', 'FontSize', 14);
ylim([1e-5 1]);
set(gca, 'YTick', [1e-5 1e-4 1e-3 1e-2 1e-1 1e0], ...
         'YTickLabel', {'10^{-5}','10^{-4}','10^{-3}','10^{-2}','10^{-1}','10^{0}'}, ...
         'TickLabelInterpreter', 'tex');

if ~isempty(legend_entries)
    legend(legend_entries, 'Location', 'northeast', 'FontSize', 10, ...
           'Interpreter', 'none');
end

end % function plot_ber


% -----------------------------------------------------------------------
function ber = theory_ber(mod_name, snr_db)

ebn0 = 10.^(snr_db/10);

if strcmp(mod_name, 'QPSK')
    ber = 0.5 * erfc(sqrt(ebn0));   % P_b = Q(sqrt(2*Eb/N0))
    return;
end

M = 0;
if     strcmp(mod_name, '16QAM'),  M = 16;
elseif strcmp(mod_name, '64QAM'),  M = 64;
elseif strcmp(mod_name, '256QAM'), M = 256;
end

if M == 0, ber = []; return; end

k     = log2(M);
coeff = (4/k) * (1 - 1/sqrt(M));
ber   = coeff * 0.5 .* erfc(sqrt(3*k*ebn0 / (2*(M-1))));
end


% -----------------------------------------------------------------------
function [snr_vec, ber_vec, block_size, label, mod_name] = parse_ber_output(filename)

fid = fopen(filename, 'r');
snr_vec    = [];
ber_vec    = [];
block_size = 0;
label      = filename;
mod_name   = '';
ch_name    = '';

while ~feof(fid)
    line = fgetl(fid);
    if ~ischar(line), break; end
    line = strtrim(line);

    tok = regexp(line, 'Modulation\s*:\s*(\S+)', 'tokens');
    if ~isempty(tok), mod_name = tok{1}{1}; end

    tok = regexp(line, 'Channel\s*:\s*(\S+)', 'tokens');
    if ~isempty(tok), ch_name = tok{1}{1}; end

    % 2-column format: Eb/N0  BER
    nums = sscanf(line, '%f %f');
    if numel(nums) == 2
        snr_vec(end+1) = nums(1); %#ok<AGROW>
        ber_vec(end+1) = nums(2); %#ok<AGROW>
    end
end
fclose(fid);

if ~isempty(mod_name)
    label = mod_name;
    if ~isempty(ch_name), label = [label ' | ' ch_name]; end
end

end % function parse_ber_output

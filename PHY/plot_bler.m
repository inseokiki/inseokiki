% plot_bler.m  -  5G NR PHY LLS BLER vs SNR plotter
%
% Usage:
%   1. Save simulator output:
%        ./lls_sim > result.txt
%   2. In MATLAB (PHY directory):
%        plot_bler              % reads 'result.txt'
%        plot_bler('myfile.txt')
%        plot_bler('a.txt', 'b.txt', 'c.txt')  % compare multiple runs

function plot_bler(varargin)

script_dir = fileparts(mfilename('fullpath'));

if nargin == 0
    files = { fullfile(script_dir, 'result_QPSK.txt'), ...
              fullfile(script_dir, 'result_16QAM.txt'), ...
              fullfile(script_dir, 'result_64QAM.txt') };
else
    files = varargin;
end

colors  = lines(numel(files));
markers = {'o', 's', '^', 'd', 'v', 'p', 'h'};

figure('Name', 'BLER vs SNR  |  5G NR PHY LLS', 'NumberTitle', 'off', ...
       'Position', [200 200 800 550]);
hold on;

legend_entries = {};

for fi = 1:numel(files)
    fname = files{fi};
    if ~isfile(fname)
        warning('File not found: %s  (skipping)', fname);
        continue;
    end

    [snr, bler, ber, block_size, lbl] = parse_lls_output(fname);

    if isempty(snr)
        warning('No data parsed from: %s', fname);
        continue;
    end

    mk = markers{mod(fi-1, numel(markers)) + 1};
    c  = colors(fi,:);

    % Simulated BLER (solid)
    semilogy(snr, max(bler, 1e-5), ...
             'Color', c, 'LineWidth', 2, ...
             'Marker', mk, 'MarkerSize', 7, 'MarkerFaceColor', c);
    legend_entries{end+1} = [lbl ' BLER'];

    % Simulated BER (dashed, same colour)
    semilogy(snr, max(ber, 1e-5), '--', ...
             'Color', c, 'LineWidth', 1.2, ...
             'Marker', mk, 'MarkerSize', 5, 'MarkerFaceColor', 'none');
    legend_entries{end+1} = [lbl ' BER'];
end

% Overlay theory curves for each modulation found
mods_plotted = {};
for fi = 1:numel(files)
    fname = files{fi};
    if ~isfile(fname), continue; end
    [~, ~, ~, block_size, ~, mod_name] = parse_lls_output(fname);
    if ~isempty(mod_name) && ~any(strcmp(mods_plotted, mod_name))
        mods_plotted{end+1} = mod_name;
        snr_th  = linspace(-5, 35, 500);
        ber_th  = theory_ber(mod_name, snr_th);
        if isempty(ber_th), continue; end

        % Theory BER (gray dotted)
        semilogy(snr_th, max(ber_th, 1e-5), ':', ...
                 'Color', [0.5 0.5 0.5], 'LineWidth', 1.2);
        legend_entries{end+1} = [mod_name ' BER theory'];

        % Theory BLER (gray dash-dot) — only if block size is known
        if block_size > 0
            bler_th = 1 - (1 - ber_th).^block_size;
            semilogy(snr_th, max(bler_th, 1e-5), '-.', ...
                     'Color', [0.5 0.5 0.5], 'LineWidth', 1.2);
            legend_entries{end+1} = sprintf('%s BLER theory (N=%d)', mod_name, block_size);
        end
    end
end

hold off;
grid on;
set(gca, 'YMinorGrid', 'on', 'FontSize', 11);
xlabel('SNR (dB)',  'FontSize', 13);
ylabel('BLER / BER', 'FontSize', 13);
title('BLER vs SNR  —  5G NR PHY Link Level Simulation', 'FontSize', 14);
ylim([1e-4 1.5]);
set(gca, 'YTick', [1e-4 1e-3 1e-2 1e-1 1e0], ...
         'YTickLabel', {'10^{-4}','10^{-3}','10^{-2}','10^{-1}','10^{0}'}, ...
         'TickLabelInterpreter', 'tex');

if ~isempty(legend_entries)
    legend(legend_entries, 'Location', 'southwest', 'FontSize', 10, ...
           'Interpreter', 'none');
end

end % function plot_bler


% -----------------------------------------------------------------------
% Theoretical uncoded BER for Gray-coded square M-QAM in AWGN (Es/N0)
% Formula: BER = (4/log2(M))*(1-1/sqrt(M))*Q(sqrt(3*snr/(M-1)))
function ber = theory_ber(mod_name, snr_db)

M = 0;
if     strcmp(mod_name, 'QPSK'),   M = 4;
elseif strcmp(mod_name, '16QAM'),  M = 16;
elseif strcmp(mod_name, '64QAM'),  M = 64;
elseif strcmp(mod_name, '256QAM'), M = 256;
end

if M == 0
    ber = [];
    return;
end

snr_lin = 10.^(snr_db/10);
coeff   = (4/log2(M)) * (1 - 1/sqrt(M));
ber     = coeff * 0.5 .* erfc(sqrt(3*snr_lin / (2*(M-1))));
end


% -----------------------------------------------------------------------
function [snr_vec, bler_vec, ber_vec, block_size, label, mod_name] = parse_lls_output(filename)

fid = fopen(filename, 'r');
snr_vec    = [];
bler_vec   = [];
ber_vec    = [];
block_size = 0;
label      = filename;
mod_name   = '';

cfg = struct('mcs', '', 'mod', '', 'ch', '', 'eq', '');

while ~feof(fid)
    line = fgetl(fid);
    if ~ischar(line), break; end
    line = strtrim(line);

    % -- collect config tokens for legend label --
    tok = regexp(line, 'MCS Index\s*:\s*(\S+)', 'tokens');
    if ~isempty(tok), cfg.mcs = ['MCS ' tok{1}{1}]; end

    tok = regexp(line, 'Modulation\s*:\s*(\S+)', 'tokens');
    if ~isempty(tok), cfg.mod = tok{1}{1}; end

    tok = regexp(line, 'Channel\s*:\s*(AWGN|Flat Rayleigh[^(]*|NONE)', 'tokens');
    if ~isempty(tok), cfg.ch = strtrim(tok{1}{1}); end

    tok = regexp(line, 'Equalizer\s*:\s*(\w+)', 'tokens');
    if ~isempty(tok), cfg.eq = tok{1}{1}; end

    tok = regexp(line, 'Block Size\s*:\s*(\d+)', 'tokens');
    if ~isempty(tok), block_size = str2double(tok{1}{1}); end

    % -- parse data lines: expect exactly 3 numbers --
    nums = sscanf(line, '%f %f %f');
    if numel(nums) == 3
        snr_vec(end+1)  = nums(1); %#ok<AGROW>
        ber_vec(end+1)  = nums(2); %#ok<AGROW>
        bler_vec(end+1) = nums(3); %#ok<AGROW>
    end
end
fclose(fid);

% Build label from collected config tokens
parts = {};
if ~isempty(cfg.mcs), parts{end+1} = cfg.mcs; end
if ~isempty(cfg.mod), parts{end+1} = cfg.mod;  end
if ~isempty(cfg.ch),  parts{end+1} = cfg.ch;   end
if ~isempty(cfg.eq),  parts{end+1} = cfg.eq;   end

if ~isempty(parts)
    label = strjoin(parts, ' | ');
end
mod_name = cfg.mod;

end % function parse_lls_output

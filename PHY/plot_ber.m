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
    files = {fullfile(script_dir, 'ber_result.txt')};
else
    files = varargin;
end

colors  = lines(numel(files));

figure('Name', 'BER vs SNR', 'NumberTitle', 'off', ...
       'Position', [200 200 800 550]);
hold on;

legend_entries = {};
first_mod = '';

for fi = 1:numel(files)
    fname = files{fi};
    if ~isfile(fname)
        warning('File not found: %s  (skipping)', fname);
        continue;
    end

    [snr, ber, ~, ~, mod_name] = parse_ber_output(fname);

    if isempty(snr)
        warning('No data parsed from: %s', fname);
        continue;
    end

    if isempty(first_mod), first_mod = mod_name; end

    c = colors(fi,:);

    % Drop BER=0 points (below measurement floor)
    valid = ber > 0;
    snr_v = snr(valid);
    ber_v = ber(valid);

    % Simulated BER — x marker
    semilogy(snr_v, ber_v, '-x', ...
             'Color', c, 'LineWidth', 1.5, 'MarkerSize', 10);
    legend_entries{end+1} = 'Simulation';

    % Theory BER overlay
    snr_th = linspace(-2, 12, 500);
    ber_th = theory_ber(mod_name, snr_th);
    if ~isempty(ber_th)
        semilogy(snr_th, ber_th, '-', ...
                 'Color', [0.8 0 0], 'LineWidth', 1.5);
        legend_entries{end+1} = 'Theory';
    end
end

hold off;
grid on;
set(gca, 'GridLineStyle', ':', 'YMinorGrid', 'on', 'FontSize', 11);

xlabel('Eb/No (dB)',    'FontSize', 13);
ylabel('Bit error rate', 'FontSize', 13);

if ~isempty(first_mod)
    title([first_mod ' bit error rate'], 'FontSize', 14);
else
    title('Bit error rate', 'FontSize', 14);
end

set(gca, 'YScale', 'log', ...
         'XLim', [0 10], ...
         'YLim', [1e-6 1e-1], ...
         'YLimMode', 'manual', ...
         'YTick', [1e-6 1e-5 1e-4 1e-3 1e-2 1e-1], ...
         'YTickLabel', {'10^{-6}','10^{-5}','10^{-4}','10^{-3}','10^{-2}','10^{-1}'}, ...
         'TickLabelInterpreter', 'tex');

if ~isempty(legend_entries)
    legend(legend_entries, 'Location', 'southwest', 'FontSize', 10);
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

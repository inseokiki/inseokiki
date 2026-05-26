% plot_all_mods.m  -  QPSK / 16QAM / 64QAM 각각 별도 figure로 표시

script_dir = fileparts(mfilename('fullpath'));

mods = {'QPSK', '16QAM', '64QAM'};

for i = 1:numel(mods)
    f = fullfile(script_dir, ['result_' mods{i} '.txt']);
    if ~isfile(f)
        fprintf('[WARN] Not found: %s\n', f);
        continue;
    end
    fprintf('[OK] Loading: %s\n', f);
    plot_bler(f);
end

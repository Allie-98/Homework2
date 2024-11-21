T=100;
% Carica il file .db3 con ros2bagreader
bag = ros2bagreader('C:\Users\bambr\Desktop\rosbag2_2024_11_19-12_25_20\rosbag2_2024_11_19-12_25_20\rosbag2_2024_11_19-12_25_20_0.db3')
% Visualizza i topic disponibili nel bag file
topicList = bag.AvailableTopics;
disp(topicList);

% Seleziona il topic che contiene le torques
msgs = readMessages(select(bag, 'Topic', '/effort_controller/commands'));


% Numero di messaggi letti
n = numel(msgs);

% Pre-allocazione per i dati
torqueValues = zeros(n, 7);  % Poiché hai 7 torques per messaggio

% Estrazione dei valori di torque (assumendo che ci siano 7 valori di torque per messaggio)
for i = 1:n
    torqueValues(i, :) = msgs{i}.data';  % Transponi per ottenere una riga di valori
end
tempo=bag.EndTime-bag.StartTime;
T=tempo/n;
% Crea un asse temporale fittizio (un indice crescente)
timeFittizio = (0:n-1)*T;

% Plottaggio dei dati
figure;  % Apre una nuova finestra di figura
hold on;  % Mantiene il grafico per aggiungere più curve

% Plot di tutte le torques (da 1 a 7) su linee separate
for j = 1:7
    plot(timeFittizio, torqueValues(:, j), 'DisplayName', ['Torque ' num2str(j)]);
end

% Etichettatura

xlabel('Time');
ylabel('Torque values');
title('Torque values in time');
legend show;  % Mostra la legenda per identificare ogni curva
grid on;
hold off;  % Rilascia il grafico